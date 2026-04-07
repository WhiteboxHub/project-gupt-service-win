#pragma once

#include "../common/protocol.h"
#include "../common/logger.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#include <memory>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// ============================================================================
// Screen Capture using DXGI Desktop Duplication API
// ============================================================================

struct CaptureConfig {
    uint32_t adapterIndex = 0;      // GPU adapter index (0 = primary)
    uint32_t outputIndex = 0;       // Monitor index (0 = primary)
    bool captureMouseCursor = true; // Include mouse cursor
    uint32_t timeoutMs = 16;        // Frame acquisition timeout (16ms = 60Hz)
};

struct FrameInfo {
    uint64_t frameNumber;           // Monotonic frame counter
    uint64_t timestampUs;           // Capture timestamp (microseconds)
    uint32_t width;                 // Frame width
    uint32_t height;                // Frame height
    DXGI_FORMAT format;             // Pixel format
    bool hasFrameUpdate;            // True if frame content changed
    uint32_t dirtyRectCount;        // Number of dirty regions
    std::vector<RECT> dirtyRects;   // Changed regions
};

class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();

    // Delete copy/move constructors
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // Initialize capture system
    bool Initialize(const CaptureConfig& config = CaptureConfig());

    // Shutdown and cleanup
    void Shutdown();

    // Capture next frame (non-blocking with timeout)
    // Returns: true if successful, false on error
    // outTexture: D3D11 texture containing captured frame (valid until ReleaseFrame)
    // outInfo: Information about captured frame
    bool CaptureFrame(ID3D11Texture2D** outTexture, FrameInfo& outInfo);

    // Release the currently held frame (must call before next CaptureFrame)
    void ReleaseFrame();

    // Get current screen dimensions
    void GetDimensions(uint32_t& outWidth, uint32_t& outHeight) const;

    // Check if initialized
    bool IsInitialized() const { return initialized; }

    // Get D3D11 device (for encoder integration)
    ID3D11Device* GetDevice() const { return d3dDevice; }
    ID3D11DeviceContext* GetContext() const { return d3dContext; }

    // Enumerate available outputs (monitors)
    static std::vector<std::wstring> EnumerateOutputs(uint32_t adapterIndex = 0);

private:
    // Initialize D3D11 device
    bool InitializeD3D11();

    // Initialize DXGI output duplication
    bool InitializeDuplication();

    // Cleanup D3D11 resources
    void CleanupD3D11();

    // Cleanup duplication resources
    void CleanupDuplication();

    // Handle access lost error (recreate duplication)
    bool HandleAccessLost();

    // Process dirty rects metadata
    bool ProcessDirtyRects(DXGI_OUTDUPL_FRAME_INFO& frameInfo, FrameInfo& outInfo);

    // Process moved rects metadata
    bool ProcessMovedRects(DXGI_OUTDUPL_FRAME_INFO& frameInfo);

private:
    // Configuration
    CaptureConfig config;

    // Initialization state
    bool initialized;
    bool frameAcquired;

    // D3D11 resources
    ID3D11Device* d3dDevice;
    ID3D11DeviceContext* d3dContext;
    ID3D11Texture2D* stagingTexture;        // For CPU access (if needed)

    // DXGI resources
    IDXGIAdapter1* dxgiAdapter;
    IDXGIOutput* dxgiOutput;
    IDXGIOutput1* dxgiOutput1;
    IDXGIOutputDuplication* deskDupl;

    // Frame information
    DXGI_OUTPUT_DESC outputDesc;
    D3D11_TEXTURE2D_DESC textureDesc;
    uint64_t frameCounter;

    // Dirty rect buffers
    std::vector<uint8_t> dirtyRectBuffer;
    std::vector<DXGI_OUTDUPL_MOVE_RECT> moveRectBuffer;
};

// ============================================================================
// Helper Functions
// ============================================================================

namespace CaptureUtils {

    // Get DXGI format name
    const char* GetDXGIFormatName(DXGI_FORMAT format);

    // Check if format is supported for encoding
    bool IsSupportedFormat(DXGI_FORMAT format);

    // Save texture to BMP file (for debugging)
    bool SaveTextureToBMP(ID3D11Device* device, ID3D11DeviceContext* context,
                          ID3D11Texture2D* texture, const wchar_t* filename);

    // Get monitor info
    struct MonitorInfo {
        std::wstring deviceName;
        RECT coordinates;
        bool isPrimary;
        uint32_t width;
        uint32_t height;
    };

    std::vector<MonitorInfo> GetMonitorInfo();

} // namespace CaptureUtils
