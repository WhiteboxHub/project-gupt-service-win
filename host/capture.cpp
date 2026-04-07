#include "capture.h"
#include "../common/utils.h"
#include <sstream>
#include <algorithm>

// ============================================================================
// ScreenCapture Implementation
// ============================================================================

ScreenCapture::ScreenCapture()
    : initialized(false)
    , frameAcquired(false)
    , d3dDevice(nullptr)
    , d3dContext(nullptr)
    , stagingTexture(nullptr)
    , dxgiAdapter(nullptr)
    , dxgiOutput(nullptr)
    , dxgiOutput1(nullptr)
    , deskDupl(nullptr)
    , frameCounter(0)
{
    ZeroMemory(&outputDesc, sizeof(outputDesc));
    ZeroMemory(&textureDesc, sizeof(textureDesc));
}

ScreenCapture::~ScreenCapture() {
    Shutdown();
}

bool ScreenCapture::Initialize(const CaptureConfig& cfg) {
    if (initialized) {
        LOG_WARNING("ScreenCapture already initialized");
        return true;
    }

    config = cfg;
    LOG_INFO("Initializing screen capture...");

    // Initialize D3D11
    if (!InitializeD3D11()) {
        LOG_ERROR("Failed to initialize D3D11");
        return false;
    }

    // Initialize DXGI duplication
    if (!InitializeDuplication()) {
        LOG_ERROR("Failed to initialize DXGI duplication");
        CleanupD3D11();
        return false;
    }

    initialized = true;
    frameCounter = 0;

    LOG_INFO_FMT("Screen capture initialized successfully: %ux%u",
                 outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left,
                 outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top);

    return true;
}

void ScreenCapture::Shutdown() {
    if (!initialized) {
        return;
    }

    LOG_INFO("Shutting down screen capture...");

    if (frameAcquired) {
        ReleaseFrame();
    }

    CleanupDuplication();
    CleanupD3D11();

    initialized = false;
    LOG_INFO("Screen capture shutdown complete");
}

bool ScreenCapture::InitializeD3D11() {
    HRESULT hr;

    // Create DXGI factory
    IDXGIFactory1* dxgiFactory = nullptr;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&dxgiFactory);
    if (FAILED(hr)) {
        LOG_ERROR_FMT("CreateDXGIFactory1 failed: 0x%08X", hr);
        return false;
    }

    // Enumerate adapters
    hr = dxgiFactory->EnumAdapters1(config.adapterIndex, &dxgiAdapter);
    dxgiFactory->Release();

    if (FAILED(hr)) {
        LOG_ERROR_FMT("EnumAdapters1 failed: 0x%08X", hr);
        return false;
    }

    // Get adapter description
    DXGI_ADAPTER_DESC1 adapterDesc;
    dxgiAdapter->GetDesc1(&adapterDesc);
    LOG_INFO_FMT("Using adapter: %ls", adapterDesc.Description);

    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        dxgiAdapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        0,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &d3dDevice,
        &featureLevel,
        &d3dContext
    );

    if (FAILED(hr)) {
        LOG_ERROR_FMT("D3D11CreateDevice failed: 0x%08X", hr);
        dxgiAdapter->Release();
        dxgiAdapter = nullptr;
        return false;
    }

    LOG_INFO_FMT("D3D11 device created with feature level: 0x%04X", featureLevel);
    return true;
}

bool ScreenCapture::InitializeDuplication() {
    HRESULT hr;

    // Enumerate outputs (monitors)
    hr = dxgiAdapter->EnumOutputs(config.outputIndex, &dxgiOutput);
    if (FAILED(hr)) {
        LOG_ERROR_FMT("EnumOutputs failed: 0x%08X (output index: %u)", hr, config.outputIndex);
        return false;
    }

    // Get output description
    dxgiOutput->GetDesc(&outputDesc);
    LOG_INFO_FMT("Output: %ls", outputDesc.DeviceName);
    LOG_INFO_FMT("Desktop coordinates: (%d, %d) - (%d, %d)",
                 outputDesc.DesktopCoordinates.left,
                 outputDesc.DesktopCoordinates.top,
                 outputDesc.DesktopCoordinates.right,
                 outputDesc.DesktopCoordinates.bottom);

    // Get IDXGIOutput1 interface
    hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1);
    if (FAILED(hr)) {
        LOG_ERROR_FMT("QueryInterface(IDXGIOutput1) failed: 0x%08X", hr);
        return false;
    }

    // Create desktop duplication
    hr = dxgiOutput1->DuplicateOutput(d3dDevice, &deskDupl);
    if (FAILED(hr)) {
        LOG_ERROR_FMT("DuplicateOutput failed: 0x%08X", hr);
        if (hr == E_ACCESSDENIED) {
            LOG_ERROR("Access denied. Make sure no other duplication is active.");
        } else if (hr == DXGI_ERROR_UNSUPPORTED) {
            LOG_ERROR("Desktop duplication not supported on this system.");
        } else if (hr == DXGI_ERROR_SESSION_DISCONNECTED) {
            LOG_ERROR("Session is disconnected (e.g., user locked screen).");
        }
        return false;
    }

    // Get duplication description
    DXGI_OUTDUPL_DESC duplicDesc;
    deskDupl->GetDesc(&duplicDesc);
    LOG_INFO_FMT("Duplication created: %ux%u, Format: %s",
                 duplicDesc.ModeDesc.Width,
                 duplicDesc.ModeDesc.Height,
                 CaptureUtils::GetDXGIFormatName(duplicDesc.ModeDesc.Format));

    // Allocate buffers for metadata
    dirtyRectBuffer.resize(1024 * 1024); // 1MB buffer
    moveRectBuffer.resize(1024);

    return true;
}

void ScreenCapture::CleanupD3D11() {
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

    if (dxgiAdapter) {
        dxgiAdapter->Release();
        dxgiAdapter = nullptr;
    }
}

void ScreenCapture::CleanupDuplication() {
    if (deskDupl) {
        deskDupl->Release();
        deskDupl = nullptr;
    }

    if (dxgiOutput1) {
        dxgiOutput1->Release();
        dxgiOutput1 = nullptr;
    }

    if (dxgiOutput) {
        dxgiOutput->Release();
        dxgiOutput = nullptr;
    }
}

bool ScreenCapture::HandleAccessLost() {
    LOG_WARNING("Access lost, recreating desktop duplication...");

    CleanupDuplication();

    // Wait a bit before recreating
    Sleep(500);

    if (!InitializeDuplication()) {
        LOG_ERROR("Failed to recreate desktop duplication");
        return false;
    }

    LOG_INFO("Desktop duplication recreated successfully");
    return true;
}

bool ScreenCapture::CaptureFrame(ID3D11Texture2D** outTexture, FrameInfo& outInfo) {
    if (!initialized) {
        LOG_ERROR("ScreenCapture not initialized");
        return false;
    }

    if (frameAcquired) {
        LOG_ERROR("Previous frame not released");
        return false;
    }

    HRESULT hr;
    IDXGIResource* desktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ZeroMemory(&frameInfo, sizeof(frameInfo));

    // Acquire next frame
    hr = deskDupl->AcquireNextFrame(config.timeoutMs, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // No new frame available (desktop hasn't changed)
        outInfo.hasFrameUpdate = false;
        return true; // Not an error, just no update
    }

    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            // Access lost, need to recreate duplication
            if (HandleAccessLost()) {
                // Retry after recreation
                return CaptureFrame(outTexture, outInfo);
            }
            return false;
        } else if (hr == DXGI_ERROR_INVALID_CALL) {
            LOG_ERROR("AcquireNextFrame: Invalid call (previous frame not released?)");
            return false;
        } else {
            LOG_ERROR_FMT("AcquireNextFrame failed: 0x%08X", hr);
            return false;
        }
    }

    // Check if frame has updates
    if (frameInfo.LastPresentTime.QuadPart == 0) {
        // No update
        desktopResource->Release();
        deskDupl->ReleaseFrame();
        outInfo.hasFrameUpdate = false;
        return true;
    }

    // Get texture from resource
    ID3D11Texture2D* acquiredTexture = nullptr;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&acquiredTexture);
    desktopResource->Release();

    if (FAILED(hr)) {
        LOG_ERROR_FMT("QueryInterface(ID3D11Texture2D) failed: 0x%08X", hr);
        deskDupl->ReleaseFrame();
        return false;
    }

    // Get texture description
    acquiredTexture->GetDesc(&textureDesc);

    // Fill frame info
    outInfo.frameNumber = ++frameCounter;
    outInfo.timestampUs = PerformanceTimer::GetTimestampMicroseconds();
    outInfo.width = textureDesc.Width;
    outInfo.height = textureDesc.Height;
    outInfo.format = textureDesc.Format;
    outInfo.hasFrameUpdate = true;

    // Process dirty rects (changed regions)
    ProcessDirtyRects(frameInfo, outInfo);

    // Process moved rects
    ProcessMovedRects(frameInfo);

    // Return texture
    *outTexture = acquiredTexture;
    frameAcquired = true;

    return true;
}

void ScreenCapture::ReleaseFrame() {
    if (!frameAcquired) {
        return;
    }

    if (deskDupl) {
        deskDupl->ReleaseFrame();
    }

    frameAcquired = false;
}

bool ScreenCapture::ProcessDirtyRects(DXGI_OUTDUPL_FRAME_INFO& frameInfo, FrameInfo& outInfo) {
    if (frameInfo.TotalMetadataBufferSize == 0) {
        outInfo.dirtyRectCount = 0;
        return true;
    }

    HRESULT hr;
    UINT bufferSize = static_cast<UINT>(dirtyRectBuffer.size());

    // Get dirty rects
    hr = deskDupl->GetFrameDirtyRects(bufferSize, reinterpret_cast<RECT*>(dirtyRectBuffer.data()), &bufferSize);

    if (hr == DXGI_ERROR_MORE_DATA) {
        // Buffer too small, resize and retry
        dirtyRectBuffer.resize(bufferSize);
        hr = deskDupl->GetFrameDirtyRects(bufferSize, reinterpret_cast<RECT*>(dirtyRectBuffer.data()), &bufferSize);
    }

    if (FAILED(hr)) {
        LOG_WARNING_FMT("GetFrameDirtyRects failed: 0x%08X", hr);
        outInfo.dirtyRectCount = 0;
        return false;
    }

    // Copy dirty rects to output
    UINT rectCount = bufferSize / sizeof(RECT);
    outInfo.dirtyRectCount = rectCount;

    if (rectCount > 0) {
        outInfo.dirtyRects.assign(
            reinterpret_cast<RECT*>(dirtyRectBuffer.data()),
            reinterpret_cast<RECT*>(dirtyRectBuffer.data()) + rectCount
        );
    }

    return true;
}

bool ScreenCapture::ProcessMovedRects(DXGI_OUTDUPL_FRAME_INFO& frameInfo) {
    if (frameInfo.TotalMetadataBufferSize == 0) {
        return true;
    }

    HRESULT hr;
    UINT bufferSize = static_cast<UINT>(moveRectBuffer.size() * sizeof(DXGI_OUTDUPL_MOVE_RECT));

    // Get moved rects
    hr = deskDupl->GetFrameMoveRects(bufferSize, moveRectBuffer.data(), &bufferSize);

    if (hr == DXGI_ERROR_MORE_DATA) {
        // Buffer too small, resize and retry
        size_t newSize = bufferSize / sizeof(DXGI_OUTDUPL_MOVE_RECT);
        moveRectBuffer.resize(newSize);
        hr = deskDupl->GetFrameMoveRects(bufferSize, moveRectBuffer.data(), &bufferSize);
    }

    if (FAILED(hr)) {
        LOG_WARNING_FMT("GetFrameMoveRects failed: 0x%08X", hr);
        return false;
    }

    // Move rects can be used for optimization (copy instead of re-encode)
    // For now, we just log them in debug mode
    #ifdef _DEBUG
    UINT moveCount = bufferSize / sizeof(DXGI_OUTDUPL_MOVE_RECT);
    if (moveCount > 0) {
        LOG_DEBUG_FMT("Frame has %u moved regions", moveCount);
    }
    #endif

    return true;
}

void ScreenCapture::GetDimensions(uint32_t& outWidth, uint32_t& outHeight) const {
    if (initialized) {
        outWidth = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
        outHeight = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
    } else {
        outWidth = 0;
        outHeight = 0;
    }
}

std::vector<std::wstring> ScreenCapture::EnumerateOutputs(uint32_t adapterIndex) {
    std::vector<std::wstring> outputs;

    IDXGIFactory1* dxgiFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&dxgiFactory))) {
        return outputs;
    }

    IDXGIAdapter1* adapter = nullptr;
    if (FAILED(dxgiFactory->EnumAdapters1(adapterIndex, &adapter))) {
        dxgiFactory->Release();
        return outputs;
    }

    IDXGIOutput* output = nullptr;
    for (UINT i = 0; ; i++) {
        if (FAILED(adapter->EnumOutputs(i, &output))) {
            break;
        }

        DXGI_OUTPUT_DESC desc;
        output->GetDesc(&desc);
        outputs.push_back(desc.DeviceName);

        output->Release();
    }

    adapter->Release();
    dxgiFactory->Release();

    return outputs;
}

// ============================================================================
// Helper Functions Implementation
// ============================================================================

namespace CaptureUtils {

const char* GetDXGIFormatName(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
        default: return "UNKNOWN";
    }
}

bool IsSupportedFormat(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return true;
        default:
            return false;
    }
}

bool SaveTextureToBMP(ID3D11Device* device, ID3D11DeviceContext* context,
                      ID3D11Texture2D* texture, const wchar_t* filename) {
    // Get texture description
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    // Create staging texture for CPU access
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Texture2D* stagingTexture = nullptr;
    HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        return false;
    }

    // Copy texture to staging
    context->CopyResource(stagingTexture, texture);

    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        stagingTexture->Release();
        return false;
    }

    // Write BMP file
    FILE* file = nullptr;
    _wfopen_s(&file, filename, L"wb");
    bool success = false;

    if (file) {
        // BMP header
        uint32_t fileSize = 54 + (desc.Width * desc.Height * 4);
        uint32_t imageSize = desc.Width * desc.Height * 4;

        // BITMAPFILEHEADER
        uint8_t bmpFileHeader[14] = {
            'B', 'M',                           // Signature
            0, 0, 0, 0,                         // File size (filled below)
            0, 0, 0, 0,                         // Reserved
            54, 0, 0, 0                         // Offset to pixel data
        };
        *(uint32_t*)(&bmpFileHeader[2]) = fileSize;

        // BITMAPINFOHEADER
        uint8_t bmpInfoHeader[40] = {
            40, 0, 0, 0,                        // Header size
            0, 0, 0, 0,                         // Width (filled below)
            0, 0, 0, 0,                         // Height (filled below)
            1, 0,                               // Planes
            32, 0,                              // Bits per pixel
            0, 0, 0, 0,                         // Compression (none)
            0, 0, 0, 0,                         // Image size (filled below)
            0, 0, 0, 0,                         // X pixels per meter
            0, 0, 0, 0,                         // Y pixels per meter
            0, 0, 0, 0,                         // Colors used
            0, 0, 0, 0                          // Important colors
        };
        *(uint32_t*)(&bmpInfoHeader[4]) = desc.Width;
        *(int32_t*)(&bmpInfoHeader[8]) = -(int32_t)desc.Height; // Negative for top-down
        *(uint32_t*)(&bmpInfoHeader[20]) = imageSize;

        fwrite(bmpFileHeader, 1, 14, file);
        fwrite(bmpInfoHeader, 1, 40, file);

        // Write pixel data
        for (uint32_t y = 0; y < desc.Height; y++) {
            uint8_t* row = (uint8_t*)mapped.pData + (y * mapped.RowPitch);
            fwrite(row, 1, desc.Width * 4, file);
        }

        fclose(file);
        success = true;
    }

    context->Unmap(stagingTexture, 0);
    stagingTexture->Release();

    return success;
}

std::vector<MonitorInfo> GetMonitorInfo() {
    std::vector<MonitorInfo> monitors;

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
        return monitors;
    }

    IDXGIAdapter1* adapter = nullptr;
    for (UINT adapterIdx = 0; factory->EnumAdapters1(adapterIdx, &adapter) != DXGI_ERROR_NOT_FOUND; ++adapterIdx) {
        IDXGIOutput* output = nullptr;
        for (UINT outputIdx = 0; adapter->EnumOutputs(outputIdx, &output) != DXGI_ERROR_NOT_FOUND; ++outputIdx) {
            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);

            MonitorInfo info;
            info.deviceName = desc.DeviceName;
            info.coordinates = desc.DesktopCoordinates;
            info.isPrimary = (desc.DesktopCoordinates.left == 0 && desc.DesktopCoordinates.top == 0);
            info.width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
            info.height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;

            monitors.push_back(info);

            output->Release();
        }
        adapter->Release();
    }

    factory->Release();
    return monitors;
}

} // namespace CaptureUtils
