#pragma once

#include "../common/protocol.h"
#include "../common/logger.h"
#include <d3d11.h>
#include <vector>
#include <memory>

// NVENC headers (will be in third_party/nvenc/include/)
// Uncomment when NVENC SDK is installed:
// #include "nvEncodeAPI.h"

// For now, define minimal NVENC types for compilation
#ifndef NVENCAPI
typedef void* NV_ENC_INPUT_PTR;
typedef void* NV_ENC_OUTPUT_PTR;
typedef void* NV_ENC_REGISTERED_PTR;
#endif

// ============================================================================
// Video Encoder using NVIDIA NVENC
// ============================================================================

enum class EncoderType {
    NVENC,      // NVIDIA hardware encoder
    SOFTWARE    // FFmpeg x264 (fallback)
};

struct EncoderConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t frameRate = 30;
    uint32_t bitrate = 5000000;         // 5 Mbps
    uint32_t maxBitrate = 0;            // 0 = same as bitrate
    uint32_t vbvBufferSize = 0;         // 0 = auto (2 frames worth)
    uint32_t gopLength = 0xFFFFFFFF;    // Infinite GOP (no B-frames)
    uint32_t idrPeriod = 60;            // IDR frame every 2 seconds at 30fps
    bool asyncMode = true;              // Async encoding
    bool lowLatency = true;             // Ultra-low latency tuning
    EncoderType preferredType = EncoderType::NVENC;
};

struct EncodedFrame {
    std::vector<uint8_t> data;          // H.264 bitstream
    uint64_t frameNumber;               // Monotonic counter
    uint64_t timestamp;                 // Encode timestamp (microseconds)
    bool isKeyframe;                    // IDR frame
    uint32_t bitstreamSize;             // Size in bytes
};

class VideoEncoder {
public:
    VideoEncoder();
    virtual ~VideoEncoder();

    // Delete copy/move
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    // Initialize encoder
    virtual bool Initialize(ID3D11Device* device, const EncoderConfig& config) = 0;

    // Shutdown encoder
    virtual void Shutdown() = 0;

    // Encode a D3D11 texture to H.264
    virtual bool EncodeFrame(ID3D11Texture2D* texture, EncodedFrame& outFrame) = 0;

    // Force next frame to be keyframe (IDR)
    virtual void ForceKeyframe() = 0;

    // Reconfigure bitrate on-the-fly
    virtual bool Reconfigure(uint32_t newBitrate) = 0;

    // Get encoder type
    virtual EncoderType GetType() const = 0;

    // Check if initialized
    bool IsInitialized() const { return initialized; }

    // Get configuration
    const EncoderConfig& GetConfig() const { return config; }

protected:
    bool initialized;
    EncoderConfig config;
    uint64_t frameCounter;
};

// ============================================================================
// NVIDIA NVENC Encoder
// ============================================================================

class NvencEncoder : public VideoEncoder {
public:
    NvencEncoder();
    ~NvencEncoder() override;

    bool Initialize(ID3D11Device* device, const EncoderConfig& config) override;
    void Shutdown() override;
    bool EncodeFrame(ID3D11Texture2D* texture, EncodedFrame& outFrame) override;
    void ForceKeyframe() override;
    bool Reconfigure(uint32_t newBitrate) override;
    EncoderType GetType() const override { return EncoderType::NVENC; }

    // Check if NVENC is available on this system
    static bool IsAvailable();

private:
    bool LoadNvencAPI();
    bool OpenEncodeSession();
    bool CreateEncoder();
    bool AllocateIOBuffers();
    void ReleaseIOBuffers();

    // Encode implementation
    bool EncodeFrameInternal(ID3D11Texture2D* texture, EncodedFrame& outFrame);
    bool LockBitstream(void* outputBuffer, EncodedFrame& outFrame);

private:
    ID3D11Device* d3dDevice;
    ID3D11DeviceContext* d3dContext;

    // NVENC handles (opaque pointers until SDK installed)
    void* nvencEncoder;                 // NV_ENC_ENCODE_API_FUNCTION_LIST*
    void* nvencSession;                 // void* (encoder instance)

    // Input/output resources
    struct EncoderBuffer {
        void* registeredResource;       // NV_ENC_REGISTERED_PTR
        void* mappedResource;           // NV_ENC_INPUT_PTR
        void* outputBuffer;             // NV_ENC_OUTPUT_PTR
        bool inUse;
    };

    std::vector<EncoderBuffer> buffers;
    size_t bufferCount;

    bool forceIDR;
};

// ============================================================================
// Software Encoder (FFmpeg x264 fallback)
// ============================================================================

class SoftwareEncoder : public VideoEncoder {
public:
    SoftwareEncoder();
    ~SoftwareEncoder() override;

    bool Initialize(ID3D11Device* device, const EncoderConfig& config) override;
    void Shutdown() override;
    bool EncodeFrame(ID3D11Texture2D* texture, EncodedFrame& outFrame) override;
    void ForceKeyframe() override;
    bool Reconfigure(uint32_t newBitrate) override;
    EncoderType GetType() const override { return EncoderType::SOFTWARE; }

    // Check if FFmpeg is available
    static bool IsAvailable();

private:
    bool InitializeFFmpeg();
    bool ConvertTextureToYUV(ID3D11Texture2D* texture, uint8_t* yuvData);

private:
    ID3D11Device* d3dDevice;
    ID3D11DeviceContext* d3dContext;
    ID3D11Texture2D* stagingTexture;

    // FFmpeg handles (will use libavcodec)
    void* avCodecContext;               // AVCodecContext*
    void* avFrame;                      // AVFrame*
    void* avPacket;                     // AVPacket*
    void* swsContext;                   // SwsContext* (color conversion)

    bool forceIDR;
};

// ============================================================================
// Encoder Factory
// ============================================================================

class EncoderFactory {
public:
    // Create best available encoder
    static std::unique_ptr<VideoEncoder> CreateEncoder(ID3D11Device* device, const EncoderConfig& config);

    // Create specific encoder type
    static std::unique_ptr<VideoEncoder> CreateNvencEncoder(ID3D11Device* device, const EncoderConfig& config);
    static std::unique_ptr<VideoEncoder> CreateSoftwareEncoder(ID3D11Device* device, const EncoderConfig& config);

    // Query capabilities
    static bool IsNvencAvailable();
    static bool IsSoftwareAvailable();
};

// ============================================================================
// Encoder Utilities
// ============================================================================

namespace EncoderUtils {

    // Analyze H.264 bitstream
    struct BitstreamInfo {
        bool isIDR;
        bool isSPS;
        bool isPPS;
        std::vector<size_t> nalUnitOffsets;
        size_t totalSize;
    };

    BitstreamInfo AnalyzeBitstream(const uint8_t* data, size_t size);

    // Find NAL units in bitstream
    std::vector<size_t> FindNalUnits(const uint8_t* data, size_t size);

    // Check if frame is keyframe
    bool IsKeyframe(const uint8_t* data, size_t size);

    // Get recommended bitrate for resolution
    uint32_t GetRecommendedBitrate(uint32_t width, uint32_t height, uint32_t fps);

} // namespace EncoderUtils
