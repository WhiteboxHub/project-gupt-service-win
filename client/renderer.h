#pragma once

#include "../common/protocol.h"
#include "../common/logger.h"
#include "decoder.h"
#include <d3d11.h>
#include <string>

class D3DRenderer {
public:
    D3DRenderer();
    ~D3DRenderer();

    D3DRenderer(const D3DRenderer&) = delete;
    D3DRenderer& operator=(const D3DRenderer&) = delete;

    bool Initialize(const wchar_t* windowTitle, uint32_t width, uint32_t height);
    void Shutdown();
    
    bool RenderFrame(ID3D11Texture2D* frameTexture);
    void Present();
    
    bool ProcessMessages();
    bool IsWindowClosed() const { return windowClosed; }
    
    HWND GetWindowHandle() const { return hwnd; }
    ID3D11Device* GetDevice() const { return device; }

private:
    bool CreateWindow(const wchar_t* title, uint32_t width, uint32_t height);
    bool CreateD3DDevice();
    bool CreateRenderTargets();
    void CleanupRenderTargets();
    
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    HWND hwnd;
    bool windowClosed;
    
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    IDXGISwapChain* swapChain;
    ID3D11RenderTargetView* renderTargetView;
    
    ID3D11VertexShader* vertexShader;
    ID3D11PixelShader* pixelShader;
    ID3D11SamplerState* samplerState;
    ID3D11Buffer* vertexBuffer;
    
    uint32_t windowWidth;
    uint32_t windowHeight;
    bool initialized;
};
