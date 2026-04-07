#include "encoder.h"
#include "../common/utils.h"
#include <algorithm>

// ============================================================================
// VideoEncoder Base Class
// ============================================================================

VideoEncoder::VideoEncoder()
    : initialized(false)
    , frameCounter(0)
{
}

VideoEncoder::~VideoEncoder() {
}

// ============================================================================
// NVENC Encoder Implementation
// ============================================================================

NvencEncoder::NvencEncoder()
    : d3dDevice(nullptr)
    , d3dContext(nullptr)
    , nvencEncoder(nullptr)
    , nvencSession(nullptr)
    , bufferCount(3)
    , forceIDR(false)
{
}

NvencEncoder::~NvencEncoder() {
    Shutdown();
}

bool NvencEncoder::Initialize(ID3D11Device* device, const EncoderConfig& cfg) {
    if (initialized) {
        LOG_WARNING("NVENC encoder already initialized");
        return true;
    }

    if (!device) {
        LOG_ERROR("Invalid D3D11 device");
        return false;
    }

    LOG_INFO("Initializing NVENC encoder...");
    config = cfg;
    d3dDevice = device;
    d3dDevice->AddRef();
    d3dDevice->GetImmediateContext(&d3dContext);

    // NOTE: This is a simplified implementation that compiles without NVENC SDK
    // In production, you would:
    // 1. Load nvEncodeAPI.dll
    // 2. Call NvEncodeAPICreateInstance()
    // 3. Open encode session with D3D11 device
    // 4. Configure encoder parameters
    // 5. Initialize encoder
    // 6. Allocate input/output buffers

    LOG_INFO("NVENC SDK not installed - using stub implementation");
    LOG_INFO("To enable hardware encoding:");
    LOG_INFO("  1. Download NVIDIA Video Codec SDK from https://developer.nvidia.com/nvidia-video-codec-sdk");
    LOG_INFO("  2. Extract to third_party/nvenc/");
    LOG_INFO("  3. Uncomment #include \"nvEncodeAPI.h\" in encoder.h");
    LOG_INFO("  4. Rebuild project");
    LOG_WARNING("Encoder will generate dummy bitstream for testing");

    // Stub initialization - pretend we succeeded
    initialized = true;
    frameCounter = 0;

    LOG_INFO_FMT("NVENC stub initialized: %ux%u @ %u fps, %u kbps",
                 config.width, config.height, config.frameRate, config.bitrate / 1000);

    return true;
}

void NvencEncoder::Shutdown() {
    if (!initialized) {
        return;
    }

    LOG_INFO("Shutting down NVENC encoder...");

    ReleaseIOBuffers();

    // In production: destroy encoder session, unload DLL

    if (d3dContext) {
        d3dContext->Release();
        d3dContext = nullptr;
    }

    if (d3dDevice) {
        d3dDevice->Release();
        d3dDevice = nullptr;
    }

    initialized = false;
    LOG_INFO("NVENC encoder shutdown complete");
}

bool NvencEncoder::EncodeFrame(ID3D11Texture2D* texture, EncodedFrame& outFrame) {
    if (!initialized) {
        LOG_ERROR("Encoder not initialized");
        return false;
    }

    if (!texture) {
        LOG_ERROR("Invalid texture");
        return false;
    }

    ScopedTimer timer(PerformanceMetrics().encodeTimeUs);

    // Get texture description
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    // In production NVENC implementation:
    // 1. Register D3D11 texture as input resource
    // 2. Map input resource
    // 3. Setup encode picture params
    // 4. Call NvEncEncodePicture()
    // 5. Lock output bitstream
    // 6. Copy bitstream data
    // 7. Unlock bitstream
    // 8. Unmap and unregister resource

    // STUB: Generate dummy H.264 bitstream for testing
    return EncodeFrameInternal(texture, outFrame);
}

bool NvencEncoder::EncodeFrameInternal(ID3D11Texture2D* texture, EncodedFrame& outFrame) {
    // Generate a minimal valid H.264 bitstream for testing
    // This allows the rest of the system to work without NVENC SDK

    frameCounter++;
    bool isKeyframe = forceIDR || (frameCounter % config.idrPeriod == 0);
    forceIDR = false;

    outFrame.frameNumber = frameCounter;
    outFrame.timestamp = PerformanceTimer::GetTimestampMicroseconds();
    outFrame.isKeyframe = isKeyframe;

    // Create dummy H.264 NAL units
    std::vector<uint8_t> bitstream;

    if (isKeyframe) {
        // SPS NAL (Sequence Parameter Set)
        uint8_t sps[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e, 0x8d, 0x8d, 0x40, 0x50};
        bitstream.insert(bitstream.end(), sps, sps + sizeof(sps));

        // PPS NAL (Picture Parameter Set)
        uint8_t pps[] = {0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80};
        bitstream.insert(bitstream.end(), pps, pps + sizeof(pps));

        // IDR slice
        uint8_t idr[] = {0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00};
        bitstream.insert(bitstream.end(), idr, idr + sizeof(idr));
    } else {
        // P-frame slice
        uint8_t pframe[] = {0x00, 0x00, 0x00, 0x01, 0x41, 0x9a, 0x21, 0x84};
        bitstream.insert(bitstream.end(), pframe, pframe + sizeof(pframe));
    }

    // Add dummy frame data (simulating compressed frame)
    size_t estimatedSize = (config.bitrate / config.frameRate) / 8;
    size_t currentSize = bitstream.size();
    if (estimatedSize > currentSize) {
        bitstream.resize(estimatedSize, 0xFF); // Pad with dummy data
    }

    outFrame.data = std::move(bitstream);
    outFrame.bitstreamSize = static_cast<uint32_t>(outFrame.data.size());

    LOG_DEBUG_FMT("Encoded frame %llu: %u bytes, keyframe: %s",
                  frameCounter,
                  outFrame.bitstreamSize,
                  isKeyframe ? "yes" : "no");

    return true;
}

bool NvencEncoder::LockBitstream(void* outputBuffer, EncodedFrame& outFrame) {
    // In production: Lock bitstream buffer and copy data
    // For now, stub implementation
    return true;
}

void NvencEncoder::ForceKeyframe() {
    forceIDR = true;
    LOG_DEBUG("Next frame will be keyframe (IDR)");
}

bool NvencEncoder::Reconfigure(uint32_t newBitrate) {
    if (!initialized) {
        return false;
    }

    LOG_INFO_FMT("Reconfiguring bitrate: %u kbps -> %u kbps",
                 config.bitrate / 1000, newBitrate / 1000);

    config.bitrate = newBitrate;

    // In production: Call NvEncReconfigureEncoder()

    return true;
}

bool NvencEncoder::IsAvailable() {
    // Check if NVENC is available
    // In production: Try to load nvEncodeAPI.dll and check GPU capabilities
    LOG_DEBUG("Checking NVENC availability...");

    // For now, assume it's not available since SDK isn't installed
    return false;
}

bool NvencEncoder::LoadNvencAPI() {
    // Load nvEncodeAPI.dll and get function pointers
    return false;
}

bool NvencEncoder::OpenEncodeSession() {
    // Open NVENC encode session with D3D11 device
    return false;
}

bool NvencEncoder::CreateEncoder() {
    // Configure and initialize encoder
    return false;
}

bool NvencEncoder::AllocateIOBuffers() {
    // Allocate input/output buffers for encoding
    buffers.resize(bufferCount);
    for (auto& buf : buffers) {
        buf.registeredResource = nullptr;
        buf.mappedResource = nullptr;
        buf.outputBuffer = nullptr;
        buf.inUse = false;
    }
    return true;
}

void NvencEncoder::ReleaseIOBuffers() {
    // Release input/output buffers
    buffers.clear();
}

// ============================================================================
// Software Encoder Implementation
// ============================================================================

SoftwareEncoder::SoftwareEncoder()
    : d3dDevice(nullptr)
    , d3dContext(nullptr)
    , stagingTexture(nullptr)
    , avCodecContext(nullptr)
    , avFrame(nullptr)
    , avPacket(nullptr)
    , swsContext(nullptr)
    , forceIDR(false)
{
}

SoftwareEncoder::~SoftwareEncoder() {
    Shutdown();
}

bool SoftwareEncoder::Initialize(ID3D11Device* device, const EncoderConfig& cfg) {
    if (initialized) {
        LOG_WARNING("Software encoder already initialized");
        return true;
    }

    if (!device) {
        LOG_ERROR("Invalid D3D11 device");
        return false;
    }

    LOG_INFO("Initializing software encoder (x264)...");
    config = cfg;
    d3dDevice = device;
    d3dDevice->AddRef();
    d3dDevice->GetImmediateContext(&d3dContext);

    // NOTE: This is a stub implementation
    // In production with FFmpeg:
    // 1. Find x264 codec (avcodec_find_encoder(AV_CODEC_ID_H264))
    // 2. Allocate codec context
    // 3. Set parameters (size, bitrate, gop, etc.)
    // 4. Set x264 private options (tune=zerolatency, preset=ultrafast)
    // 5. Open codec
    // 6. Allocate frame and packet structures

    LOG_INFO("FFmpeg not linked - using stub implementation");
    LOG_INFO("Software encoding would provide fallback when NVENC unavailable");
    LOG_WARNING("Encoder will generate dummy bitstream for testing");

    // Create staging texture for CPU access
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = config.width;
    stagingDesc.Height = config.height;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hr = d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        LOG_ERROR_FMT("Failed to create staging texture: 0x%08X", hr);
        Shutdown();
        return false;
    }

    initialized = true;
    frameCounter = 0;

    LOG_INFO_FMT("Software encoder stub initialized: %ux%u @ %u fps, %u kbps",
                 config.width, config.height, config.frameRate, config.bitrate / 1000);

    return true;
}

void SoftwareEncoder::Shutdown() {
    if (!initialized) {
        return;
    }

    LOG_INFO("Shutting down software encoder...");

    // In production: Free FFmpeg resources

    if (stagingTexture) {
        stagingTexture->Release();
        stagingTexture = nullptr;
    }

    if (d3dContext) {
        d3dContext->Release();
        d3dContext = nullptr;
    }

    if (d3dDevice) {
        d3dDevice->Release();
        d3dDevice = nullptr;
    }

    initialized = false;
    LOG_INFO("Software encoder shutdown complete");
}

bool SoftwareEncoder::EncodeFrame(ID3D11Texture2D* texture, EncodedFrame& outFrame) {
    if (!initialized) {
        LOG_ERROR("Encoder not initialized");
        return false;
    }

    if (!texture) {
        LOG_ERROR("Invalid texture");
        return false;
    }

    ScopedTimer timer(PerformanceMetrics().encodeTimeUs);

    // In production:
    // 1. Copy texture to staging texture
    // 2. Map staging texture for CPU read
    // 3. Convert BGRA to YUV420 (using swscale)
    // 4. Copy YUV data to AVFrame
    // 5. Encode frame (avcodec_send_frame)
    // 6. Receive packet (avcodec_receive_packet)
    // 7. Copy packet data to output

    // STUB: Generate dummy bitstream
    frameCounter++;
    bool isKeyframe = forceIDR || (frameCounter % config.idrPeriod == 0);
    forceIDR = false;

    outFrame.frameNumber = frameCounter;
    outFrame.timestamp = PerformanceTimer::GetTimestampMicroseconds();
    outFrame.isKeyframe = isKeyframe;

    // Dummy bitstream (same as NVENC stub)
    std::vector<uint8_t> bitstream;
    if (isKeyframe) {
        uint8_t sps[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e, 0x8d, 0x8d, 0x40, 0x50};
        uint8_t pps[] = {0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80};
        uint8_t idr[] = {0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00};
        bitstream.insert(bitstream.end(), sps, sps + sizeof(sps));
        bitstream.insert(bitstream.end(), pps, pps + sizeof(pps));
        bitstream.insert(bitstream.end(), idr, idr + sizeof(idr));
    } else {
        uint8_t pframe[] = {0x00, 0x00, 0x00, 0x01, 0x41, 0x9a, 0x21, 0x84};
        bitstream.insert(bitstream.end(), pframe, pframe + sizeof(pframe));
    }

    size_t estimatedSize = (config.bitrate / config.frameRate) / 8;
    if (estimatedSize > bitstream.size()) {
        bitstream.resize(estimatedSize, 0xFF);
    }

    outFrame.data = std::move(bitstream);
    outFrame.bitstreamSize = static_cast<uint32_t>(outFrame.data.size());

    return true;
}

bool SoftwareEncoder::InitializeFFmpeg() {
    // Initialize FFmpeg codec
    return false;
}

bool SoftwareEncoder::ConvertTextureToYUV(ID3D11Texture2D* texture, uint8_t* yuvData) {
    // Copy texture to staging, map, convert BGRA to YUV420
    d3dContext->CopyResource(stagingTexture, texture);

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = d3dContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        return false;
    }

    // In production: Use swscale to convert BGRA to YUV420

    d3dContext->Unmap(stagingTexture, 0);
    return true;
}

void SoftwareEncoder::ForceKeyframe() {
    forceIDR = true;
}

bool SoftwareEncoder::Reconfigure(uint32_t newBitrate) {
    if (!initialized) {
        return false;
    }

    config.bitrate = newBitrate;
    return true;
}

bool SoftwareEncoder::IsAvailable() {
    // Check if FFmpeg is available
    return false;
}

// ============================================================================
// Encoder Factory
// ============================================================================

std::unique_ptr<VideoEncoder> EncoderFactory::CreateEncoder(ID3D11Device* device, const EncoderConfig& config) {
    LOG_INFO("Creating encoder...");

    // Try NVENC first if preferred
    if (config.preferredType == EncoderType::NVENC) {
        if (IsNvencAvailable()) {
            LOG_INFO("Using NVENC hardware encoder");
            return CreateNvencEncoder(device, config);
        } else {
            LOG_WARNING("NVENC not available, falling back to software encoder");
        }
    }

    // Fall back to software encoder
    if (IsSoftwareAvailable()) {
        LOG_INFO("Using software encoder (x264)");
        return CreateSoftwareEncoder(device, config);
    }

    // If nothing available, still create stub encoder for testing
    LOG_WARNING("No encoder available, creating stub NVENC encoder for testing");
    return CreateNvencEncoder(device, config);
}

std::unique_ptr<VideoEncoder> EncoderFactory::CreateNvencEncoder(ID3D11Device* device, const EncoderConfig& config) {
    auto encoder = std::make_unique<NvencEncoder>();
    if (encoder->Initialize(device, config)) {
        return encoder;
    }
    return nullptr;
}

std::unique_ptr<VideoEncoder> EncoderFactory::CreateSoftwareEncoder(ID3D11Device* device, const EncoderConfig& config) {
    auto encoder = std::make_unique<SoftwareEncoder>();
    if (encoder->Initialize(device, config)) {
        return encoder;
    }
    return nullptr;
}

bool EncoderFactory::IsNvencAvailable() {
    return NvencEncoder::IsAvailable();
}

bool EncoderFactory::IsSoftwareAvailable() {
    return SoftwareEncoder::IsAvailable();
}

// ============================================================================
// Encoder Utilities
// ============================================================================

namespace EncoderUtils {

BitstreamInfo AnalyzeBitstream(const uint8_t* data, size_t size) {
    BitstreamInfo info = {};
    info.totalSize = size;

    auto nalUnits = FindNalUnits(data, size);
    info.nalUnitOffsets = nalUnits;

    for (size_t offset : nalUnits) {
        if (offset + 5 > size) continue;

        uint8_t nalType = data[offset + 4] & 0x1F;

        if (nalType == 5) info.isIDR = true;       // IDR slice
        if (nalType == 7) info.isSPS = true;       // SPS
        if (nalType == 8) info.isPPS = true;       // PPS
    }

    return info;
}

std::vector<size_t> FindNalUnits(const uint8_t* data, size_t size) {
    std::vector<size_t> offsets;

    for (size_t i = 0; i < size - 4; i++) {
        // Look for start code: 0x00 0x00 0x00 0x01
        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
            data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            offsets.push_back(i);
            i += 3; // Skip start code
        }
        // Or 3-byte start code: 0x00 0x00 0x01
        else if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            offsets.push_back(i);
            i += 2;
        }
    }

    return offsets;
}

bool IsKeyframe(const uint8_t* data, size_t size) {
    auto nalUnits = FindNalUnits(data, size);

    for (size_t offset : nalUnits) {
        if (offset + 5 > size) continue;

        uint8_t nalType = data[offset + 4] & 0x1F;
        if (nalType == 5) { // IDR slice
            return true;
        }
    }

    return false;
}

uint32_t GetRecommendedBitrate(uint32_t width, uint32_t height, uint32_t fps) {
    // Simple bitrate estimation based on resolution and fps
    uint32_t pixels = width * height;
    double bitsPerPixel;

    if (pixels <= 1280 * 720) {
        bitsPerPixel = 0.1; // 720p: ~0.1 bpp
    } else if (pixels <= 1920 * 1080) {
        bitsPerPixel = 0.08; // 1080p: ~0.08 bpp
    } else if (pixels <= 2560 * 1440) {
        bitsPerPixel = 0.06; // 1440p: ~0.06 bpp
    } else {
        bitsPerPixel = 0.05; // 4K: ~0.05 bpp
    }

    uint32_t bitrate = static_cast<uint32_t>(pixels * fps * bitsPerPixel);

    // Clamp to reasonable range
    bitrate = std::max(bitrate, 1000000u);   // Min 1 Mbps
    bitrate = std::min(bitrate, 50000000u);  // Max 50 Mbps

    return bitrate;
}

} // namespace EncoderUtils
