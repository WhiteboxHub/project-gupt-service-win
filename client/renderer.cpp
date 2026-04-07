#include "renderer.h"
#include "../common/utils.h"
#include <windowsx.h>

D3DRenderer::D3DRenderer()
    : hwnd(nullptr), windowClosed(false), device(nullptr), context(nullptr)
    , swapChain(nullptr), renderTargetView(nullptr), vertexShader(nullptr)
    , pixelShader(nullptr), samplerState(nullptr), vertexBuffer(nullptr)
    , windowWidth(0), windowHeight(0), initialized(false) {}

D3DRenderer::~D3DRenderer() { Shutdown(); }

bool D3DRenderer::Initialize(const wchar_t* windowTitle, uint32_t width, uint32_t height) {
    if (initialized) {
        LOG_WARNING("Renderer already initialized");
        return true;
    }

    LOG_INFO("Initializing D3D renderer...");
    windowWidth = width;
    windowHeight = height;

    if (!CreateWindow(windowTitle, width, height)) {
        LOG_ERROR("Failed to create window");
        return false;
    }

    if (!CreateD3DDevice()) {
        LOG_ERROR("Failed to create D3D device");
        DestroyWindow(hwnd);
        return false;
    }

    if (!CreateRenderTargets()) {
        LOG_ERROR("Failed to create render targets");
        Shutdown();
        return false;
    }

    initialized = true;
    LOG_INFO_FMT("D3D renderer initialized: %ux%u", width, height);
    return true;
}

void D3DRenderer::Shutdown() {
    if (!initialized) return;

    LOG_INFO("Shutting down D3D renderer...");

    CleanupRenderTargets();

    if (vertexBuffer) {
        vertexBuffer->Release();
        vertexBuffer = nullptr;
    }
    if (samplerState) {
        samplerState->Release();
        samplerState = nullptr;
    }
    if (pixelShader) {
        pixelShader->Release();
        pixelShader = nullptr;
    }
    if (vertexShader) {
        vertexShader->Release();
        vertexShader = nullptr;
    }
    if (swapChain) {
        swapChain->Release();
        swapChain = nullptr;
    }
    if (context) {
        context->Release();
        context = nullptr;
    }
    if (device) {
        device->Release();
        device = nullptr;
    }

    if (hwnd) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }

    initialized = false;
    LOG_INFO("D3D renderer shutdown complete");
}

bool D3DRenderer::CreateWindow(const wchar_t* title, uint32_t width, uint32_t height) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"GuPTClientWindow";

    if (!RegisterClassExW(&wc)) {
        LOG_ERROR("Failed to register window class");
        return false;
    }

    RECT rc = {0, 0, (LONG)width, (LONG)height};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd = CreateWindowExW(
        0, L"GuPTClientWindow", title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );

    if (!hwnd) {
        LOG_ERROR("Failed to create window");
        return false;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    LOG_INFO("Window created");
    return true;
}

bool D3DRenderer::CreateD3DDevice() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = windowWidth;
    sd.BufferDesc.Height = windowHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION, &sd, &swapChain,
        &device, &featureLevel, &context
    );

    if (FAILED(hr)) {
        LOG_ERROR_FMT("D3D11CreateDeviceAndSwapChain failed: 0x%08X", hr);
        return false;
    }

    LOG_INFO("D3D11 device created");
    return true;
}

bool D3DRenderer::CreateRenderTargets() {
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get back buffer");
        return false;
    }

    hr = device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
    backBuffer->Release();

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create render target view");
        return false;
    }

    return true;
}

void D3DRenderer::CleanupRenderTargets() {
    if (renderTargetView) {
        renderTargetView->Release();
        renderTargetView = nullptr;
    }
}

bool D3DRenderer::RenderFrame(ID3D11Texture2D* frameTexture) {
    if (!initialized || !frameTexture) {
        return false;
    }

    context->OMSetRenderTargets(1, &renderTargetView, nullptr);

    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(renderTargetView, clearColor);

    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = device->CreateShaderResourceView(frameTexture, nullptr, &srv);
    if (SUCCEEDED(hr)) {
        // Simple texture copy (in production, use proper shader pipeline)
        D3D11_VIEWPORT vp = {};
        vp.Width = (FLOAT)windowWidth;
        vp.Height = (FLOAT)windowHeight;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context->RSSetViewports(1, &vp);

        srv->Release();
    }

    return true;
}

void D3DRenderer::Present() {
    if (swapChain) {
        swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
    }
}

bool D3DRenderer::ProcessMessages() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            windowClosed = true;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return !windowClosed;
}

LRESULT CALLBACK D3DRenderer::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    D3DRenderer* renderer = nullptr;
    
    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        renderer = (D3DRenderer*)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)renderer);
    } else {
        renderer = (D3DRenderer*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    if (renderer) {
        return renderer->WndProc(hwnd, msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT D3DRenderer::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            windowClosed = true;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            if (swapChain && device) {
                CleanupRenderTargets();
                swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
                CreateRenderTargets();
            }
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
