#include "decoder.h"
#include "../common/utils.h"

VideoDecoder::VideoDecoder() : initialized(false), frameCounter(0) {}
VideoDecoder::~VideoDecoder() {}

SoftwareDecoder::SoftwareDecoder()
    : d3dDevice(nullptr), d3dContext(nullptr), outputTexture(nullptr) {}

SoftwareDecoder::~SoftwareDecoder() { Shutdown(); }

bool SoftwareDecoder::Initialize(ID3D11Device* device, const DecoderConfig& cfg) {
    if (initialized) {
        LOG_WARNING("Decoder already initialized");
        return true;
    }
    if (!device) {
        LOG_ERROR("Invalid D3D11 device");
        return false;
    }

    LOG_INFO("Initializing software decoder...");
    config = cfg;
    d3dDevice = device;
    d3dDevice->AddRef();
    d3dDevice->GetImmediateContext(&d3dContext);

    LOG_INFO("FFmpeg not linked - using stub decoder");
    LOG_WARNING("Output texture will be blank");

    if (!CreateD3DTexture(config.width, config.height)) {
        LOG_ERROR("Failed to create output texture");
        Shutdown();
        return false;
    }

    initialized = true;
    frameCounter = 0;

    LOG_INFO_FMT("Software decoder stub initialized: %ux%u", config.width, config.height);
    return true;
}

void SoftwareDecoder::Shutdown() {
    if (!initialized) return;
    LOG_INFO("Shutting down software decoder...");

    if (outputTexture) {
        outputTexture->Release();
        outputTexture = nullptr;
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
    LOG_INFO("Software decoder shutdown complete");
}

bool SoftwareDecoder::DecodeFrame(const ReceivedFrame& frame, DecodedFrame& outFrame) {
    if (!initialized) {
        LOG_ERROR("Decoder not initialized");
        return false;
    }

    frameCounter++;
    outFrame.texture = outputTexture;
    outFrame.frameNumber = frame.frameNumber;
    outFrame.timestamp = frame.timestamp;
    outFrame.isKeyframe = frame.isKeyframe;

    LOG_DEBUG_FMT("Decoded frame %llu (stub)", frameCounter);
    return true;
}

bool SoftwareDecoder::CreateD3DTexture(uint32_t width, uint32_t height) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    std::vector<uint32_t> blackPixels(width * height, 0xFF000000);
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = blackPixels.data();
    initData.SysMemPitch = width * 4;

    HRESULT hr = d3dDevice->CreateTexture2D(&desc, &initData, &outputTexture);
    if (FAILED(hr)) {
        LOG_ERROR_FMT("Failed to create D3D11 texture: 0x%08X", hr);
        return false;
    }
    return true;
}

bool SoftwareDecoder::IsAvailable() { return false; }

std::unique_ptr<VideoDecoder> DecoderFactory::CreateDecoder(ID3D11Device* device, const DecoderConfig& config) {
    LOG_INFO("Creating decoder...");
    LOG_INFO("Using software decoder (FFmpeg stub)");

    auto decoder = std::make_unique<SoftwareDecoder>();
    if (decoder->Initialize(device, config)) {
        return decoder;
    }
    return nullptr;
}
