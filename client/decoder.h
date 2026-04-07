#pragma once

#include "../common/protocol.h"
#include "../common/logger.h"
#include "network_client.h"
#include <d3d11.h>
#include <memory>

// ============================================================================
// Video Decoder for H.264
// ============================================================================

struct DecoderConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    bool useHardwareDecoding = true;
};

struct DecodedFrame {
    ID3D11Texture2D* texture;
    uint64_t frameNumber;
    uint64_t timestamp;
    bool isKeyframe;
};

class VideoDecoder {
public:
    VideoDecoder();
    virtual ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    virtual bool Initialize(ID3D11Device* device, const DecoderConfig& config) = 0;
    virtual void Shutdown() = 0;
    virtual bool DecodeFrame(const ReceivedFrame& frame, DecodedFrame& outFrame) = 0;

    bool IsInitialized() const { return initialized; }

protected:
    bool initialized;
    DecoderConfig config;
    uint64_t frameCounter;
};

class SoftwareDecoder : public VideoDecoder {
public:
    SoftwareDecoder();
    ~SoftwareDecoder() override;

    bool Initialize(ID3D11Device* device, const DecoderConfig& config) override;
    void Shutdown() override;
    bool DecodeFrame(const ReceivedFrame& frame, DecodedFrame& outFrame) override;

    static bool IsAvailable();

private:
    bool CreateD3DTexture(uint32_t width, uint32_t height);

    ID3D11Device* d3dDevice;
    ID3D11DeviceContext* d3dContext;
    ID3D11Texture2D* outputTexture;
};

class DecoderFactory {
public:
    static std::unique_ptr<VideoDecoder> CreateDecoder(ID3D11Device* device, const DecoderConfig& config);
};
