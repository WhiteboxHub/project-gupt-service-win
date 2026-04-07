# GUPT Remote Desktop - Implementation Plan

## Overview

This document outlines a step-by-step implementation strategy for building the GUPT remote desktop system. We'll build incrementally, testing each component before moving to the next.

---

## Phase 1: Foundation & Networking (Days 1-3)

### Objectives
- Set up project structure
- Implement basic TCP/UDP communication
- Test bidirectional data flow

### Tasks

#### 1.1 Project Setup
- [x] Create CMakeLists.txt
- [x] Configure Visual Studio solution
- [x] Set up directory structure
- [x] Add common headers (protocol.h, packet.h)
- [x] Implement logging system

#### 1.2 Common Utilities
- [ ] `ThreadSafeQueue<T>` - Lock-free SPSC queue
- [ ] `Logger` - Multi-level logging
- [ ] `PerformanceTimer` - Microsecond timing
- [ ] Packet serialization helpers

**Files to Create:**
- `common/protocol.h` - Constants, magic numbers
- `common/packet.h` - Packet structures
- `common/utils.h/.cpp` - Utility functions
- `common/logger.h/.cpp` - Logging system

#### 1.3 Basic TCP Server/Client
**Goal:** Establish reliable control channel

**Host Implementation:**
```cpp
// host/network_server.cpp
class NetworkServer {
    SOCKET tcpSocket;
    void Initialize(uint16_t port);
    void AcceptConnections();
    void HandleControlMessages();
};
```

**Client Implementation:**
```cpp
// client/network_client.cpp
class NetworkClient {
    SOCKET tcpSocket;
    bool Connect(const char* ip, uint16_t port);
    void SendControlMessage();
    void ReceiveControlMessage();
};
```

**Test Plan:**
- Run host, bind to port 5900
- Run client, connect to 127.0.0.1:5900
- Send "HELLO" from client
- Verify host receives it
- Send "WORLD" from host
- Verify client receives it

#### 1.4 Basic UDP Socket Setup
**Goal:** Set up unreliable video channel

**Host:**
```cpp
// Add to NetworkServer
SOCKET udpSocket;
void InitializeUDP(uint16_t port);
void SendVideoPacket(const uint8_t* data, size_t size);
```

**Client:**
```cpp
// Add to NetworkClient
SOCKET udpSocket;
void InitializeUDP();
void ReceiveVideoPacket(uint8_t* buffer, size_t* size);
```

**Test Plan:**
- Send 1000 dummy packets from host
- Count packets received by client
- Measure packet loss rate
- Should be <1% on localhost

**Deliverable:** Working TCP + UDP communication between host and client

---

## Phase 2: Screen Capture (Days 4-6)

### Objectives
- Implement DXGI Desktop Duplication
- Handle frame acquisition
- Manage dirty regions

### Tasks

#### 2.1 D3D11 Device Initialization
**File:** `host/capture.h/.cpp`

```cpp
class ScreenCapture {
private:
    ID3D11Device* d3dDevice;
    ID3D11DeviceContext* d3dContext;
    IDXGIOutputDuplication* deskDupl;

public:
    bool Initialize();
    bool CaptureFrame(ID3D11Texture2D** outTexture, bool& frameAvailable);
    void ReleaseFrame();
    void GetDirtyRects(std::vector<RECT>& dirtyRects);
    void Cleanup();
};
```

**Key Steps:**
1. Create D3D11 device with `D3D11_CREATE_DEVICE_FLAG`
2. Get DXGI adapter
3. Enumerate outputs (monitors)
4. Get `IDXGIOutput1` interface
5. Call `DuplicateOutput()` to get `IDXGIOutputDuplication`

#### 2.2 Frame Acquisition Loop

```cpp
// Pseudo-code for capture thread
void CaptureThread() {
    while (running) {
        ID3D11Texture2D* frame;
        bool available;

        if (capture.CaptureFrame(&frame, available)) {
            if (available) {
                // Queue frame for encoding
                encoderQueue.Push(frame);
            }
            capture.ReleaseFrame();
        } else {
            // Handle errors (access lost, mode change)
            HandleCaptureError();
        }

        Sleep(16); // ~60 FPS
    }
}
```

#### 2.3 Error Handling

**Common Errors:**
- `DXGI_ERROR_ACCESS_LOST` - UAC prompt, display mode change
  - **Solution:** Recreate duplication interface
- `DXGI_ERROR_WAIT_TIMEOUT` - No frame available
  - **Solution:** Continue loop
- `DXGI_ERROR_INVALID_CALL` - Didn't release previous frame
  - **Solution:** Call `ReleaseFrame()` before next acquire

**Implementation:**
```cpp
bool HandleAccessLost() {
    deskDupl->Release();
    deskDupl = nullptr;

    // Wait and retry
    Sleep(500);
    return Initialize(); // Recreate duplication
}
```

#### 2.4 Dirty Region Optimization

```cpp
void ProcessDirtyRects() {
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    // ... after AcquireNextFrame ...

    if (frameInfo.TotalMetadataBufferSize > 0) {
        std::vector<DXGI_OUTDUPL_MOVE_RECT> moveRects;
        std::vector<RECT> dirtyRects;

        deskDupl->GetFrameDirtyRects(
            dirtyRects.size() * sizeof(RECT),
            dirtyRects.data(),
            &actualSize
        );

        // Only encode dirty regions (advanced feature)
    }
}
```

**Test Plan:**
- Display captured frames to console (resolution, format)
- Save frame as BMP file to verify capture works
- Test with resolution change (Settings -> Display)
- Test with UAC prompt (trigger access lost)
- Measure capture time (<5ms target)

**Deliverable:** Reliable screen capture at 60 FPS

---

## Phase 3: Hardware Encoding (Days 7-10)

### Objectives
- Initialize NVENC encoder
- Encode D3D11 textures to H.264
- Optimize for low latency

### Tasks

#### 3.1 NVENC SDK Setup

**Prerequisites:**
- Download NVIDIA Video Codec SDK
- Extract to `third_party/nvenc/`
- Add `include/` to project include paths
- Link against `nvencodeapi.lib`

**Required Headers:**
```cpp
#include "nvEncodeAPI.h"
```

#### 3.2 NVENC Encoder Class

**File:** `host/encoder.h/.cpp`

```cpp
class NvencEncoder {
private:
    void* encoder;              // NV_ENCODE_API_FUNCTION_LIST
    NV_ENC_INITIALIZE_PARAMS initParams;
    NV_ENC_CONFIG encodeConfig;

    ID3D11Device* d3dDevice;    // Shared with capture

public:
    bool Initialize(uint32_t width, uint32_t height, uint32_t bitrate);
    bool EncodeFrame(ID3D11Texture2D* texture, std::vector<uint8_t>& outBitstream);
    void Reconfigure(uint32_t newBitrate);
    void ForceKeyframe();
    void Cleanup();
};
```

#### 3.3 Initialization Steps

```cpp
bool NvencEncoder::Initialize(uint32_t width, uint32_t height, uint32_t bitrate) {
    // 1. Load NVENC API
    NV_ENCODE_API_FUNCTION_LIST funcList = {NV_ENCODE_API_FUNCTION_LIST_VER};
    NvEncodeAPICreateInstance(&funcList);

    // 2. Open encode session (D3D11 mode)
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sessionParams = {};
    sessionParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    sessionParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    sessionParams.device = d3dDevice;
    sessionParams.apiVersion = NVENCAPI_VERSION;

    funcList.nvEncOpenEncodeSessionEx(&sessionParams, &encoder);

    // 3. Get encoder capabilities
    NV_ENC_CAPS_PARAM capsParam = {};
    capsParam.capsToQuery = NV_ENC_CAPS_ASYNC_ENCODE_SUPPORT;
    int asyncSupport;
    funcList.nvEncGetEncodeCaps(encoder, NV_ENC_CODEC_H264_GUID, &capsParam, &asyncSupport);

    // 4. Configure encoder
    NV_ENC_PRESET_CONFIG presetConfig = {NV_ENC_PRESET_CONFIG_VER, {NV_ENC_CONFIG_VER}};
    funcList.nvEncGetEncodePresetConfigEx(
        encoder,
        NV_ENC_CODEC_H264_GUID,
        NV_ENC_PRESET_P1_GUID,  // Ultra-low latency
        NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY,
        &presetConfig
    );

    // 5. Customize for streaming
    encodeConfig = presetConfig.presetCfg;
    encodeConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    encodeConfig.rcParams.averageBitRate = bitrate;
    encodeConfig.rcParams.maxBitRate = bitrate;
    encodeConfig.rcParams.vbvBufferSize = bitrate / 30 * 2; // 2 frames
    encodeConfig.gopLength = NVENC_INFINITE_GOPLENGTH;
    encodeConfig.frameIntervalP = 1; // No B-frames
    encodeConfig.encodeCodecConfig.h264Config.idrPeriod = 60; // IDR every 2s at 30fps

    // 6. Initialize encoder
    initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
    initParams.presetGUID = NV_ENC_PRESET_P1_GUID;
    initParams.encodeWidth = width;
    initParams.encodeHeight = height;
    initParams.darWidth = width;
    initParams.darHeight = height;
    initParams.frameRateNum = 30;
    initParams.frameRateDen = 1;
    initParams.enableEncodeAsync = asyncSupport;
    initParams.enablePTD = 1; // Picture timing SEI
    initParams.encodeConfig = &encodeConfig;

    funcList.nvEncInitializeEncoder(encoder, &initParams);

    return true;
}
```

#### 3.4 Encoding Workflow

```cpp
bool NvencEncoder::EncodeFrame(ID3D11Texture2D* texture, std::vector<uint8_t>& outBitstream) {
    // 1. Register D3D11 texture as input resource
    NV_ENC_REGISTER_RESOURCE registerRes = {NV_ENC_REGISTER_RESOURCE_VER};
    registerRes.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    registerRes.resourceToRegister = texture;
    registerRes.width = width;
    registerRes.height = height;
    registerRes.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;

    void* registeredResource;
    funcList.nvEncRegisterResource(encoder, &registerRes, &registeredResource);

    // 2. Map resource
    NV_ENC_MAP_INPUT_RESOURCE mapRes = {NV_ENC_MAP_INPUT_RESOURCE_VER};
    mapRes.registeredResource = registeredResource;
    funcList.nvEncMapInputResource(encoder, &mapRes);

    // 3. Encode
    NV_ENC_PIC_PARAMS picParams = {NV_ENC_PIC_PARAMS_VER};
    picParams.inputBuffer = mapRes.mappedResource;
    picParams.bufferFmt = NV_ENC_BUFFER_FORMAT_ARGB;
    picParams.inputWidth = width;
    picParams.inputHeight = height;
    picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

    // Create output bitstream buffer
    NV_ENC_CREATE_BITSTREAM_BUFFER createBitstream = {NV_ENC_CREATE_BITSTREAM_BUFFER_VER};
    funcList.nvEncCreateBitstreamBuffer(encoder, &createBitstream);
    picParams.outputBitstream = createBitstream.bitstreamBuffer;

    funcList.nvEncEncodePicture(encoder, &picParams);

    // 4. Lock bitstream (wait for encode to complete)
    NV_ENC_LOCK_BITSTREAM lockBitstream = {NV_ENC_LOCK_BITSTREAM_VER};
    lockBitstream.outputBitstream = picParams.outputBitstream;
    funcList.nvEncLockBitstream(encoder, &lockBitstream);

    // 5. Copy to output buffer
    outBitstream.resize(lockBitstream.bitstreamSizeInBytes);
    memcpy(outBitstream.data(), lockBitstream.bitstreamBufferPtr, lockBitstream.bitstreamSizeInBytes);

    // 6. Cleanup
    funcList.nvEncUnlockBitstream(encoder, picParams.outputBitstream);
    funcList.nvEncUnmapInputResource(encoder, mapRes.mappedResource);
    funcList.nvEncUnregisterResource(encoder, registeredResource);
    funcList.nvEncDestroyBitstreamBuffer(encoder, picParams.outputBitstream);

    return true;
}
```

#### 3.5 Software Fallback (FFmpeg)

**Optional:** If NVENC unavailable, use libx264

```cpp
// Using FFmpeg libavcodec
#include <libavcodec/avcodec.h>

class SoftwareEncoder {
    AVCodecContext* codecCtx;
    AVFrame* frame;
    AVPacket* pkt;

public:
    bool Initialize(uint32_t width, uint32_t height, uint32_t bitrate) {
        AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        codecCtx = avcodec_alloc_context3(codec);

        codecCtx->width = width;
        codecCtx->height = height;
        codecCtx->bit_rate = bitrate;
        codecCtx->time_base = {1, 30};
        codecCtx->framerate = {30, 1};
        codecCtx->gop_size = 9999; // Infinite GOP
        codecCtx->max_b_frames = 0;

        // x264 options
        av_opt_set(codecCtx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(codecCtx->priv_data, "tune", "zerolatency", 0);

        avcodec_open2(codecCtx, codec, nullptr);
        return true;
    }

    // Similar EncodeFrame method...
};
```

**Test Plan:**
- Capture frame -> Encode -> Save as .h264 file
- Play with VLC/FFplay to verify encoding works
- Measure encoding time (<10ms target)
- Test bitrate changes (reconfigure encoder)
- Force keyframe and verify IDR frame

**Deliverable:** H.264 encoded video stream from screen capture

---

## Phase 4: Video Streaming (Days 11-13)

### Objectives
- Packetize encoded frames
- Transmit via UDP
- Handle packet reordering on client

### Tasks

#### 4.1 Packet Format Design

**File:** `common/packet.h`

```cpp
#define MAX_UDP_PAYLOAD 1400  // MTU 1500 - IP(20) - UDP(8) - margin

struct VideoPacketHeader {
    uint32_t magic;           // 0x47555054 "GUPT"
    uint16_t version;         // Protocol version
    uint16_t type;            // FRAME_DATA, KEYFRAME, etc.
    uint64_t frameId;         // Monotonic frame counter
    uint32_t sequenceNum;     // Packet index within frame
    uint32_t totalPackets;    // Total packets for frame
    uint32_t dataSize;        // Payload size
    uint64_t timestamp;       // Capture timestamp (microseconds)
    uint8_t flags;            // IDR frame, last packet, etc.
    uint8_t reserved[3];
} __attribute__((packed));

struct VideoPacket {
    VideoPacketHeader header;
    uint8_t data[MAX_UDP_PAYLOAD];
};
```

#### 4.2 Packetization (Host)

```cpp
// host/network_server.cpp
void NetworkServer::SendEncodedFrame(const std::vector<uint8_t>& bitstream, uint64_t frameId, bool isKeyframe) {
    size_t totalSize = bitstream.size();
    uint32_t totalPackets = (totalSize + MAX_UDP_PAYLOAD - 1) / MAX_UDP_PAYLOAD;

    for (uint32_t i = 0; i < totalPackets; i++) {
        VideoPacket packet = {};
        packet.header.magic = GUPT_MAGIC;
        packet.header.version = PROTOCOL_VERSION;
        packet.header.type = isKeyframe ? PACKET_TYPE_KEYFRAME : PACKET_TYPE_FRAME;
        packet.header.frameId = frameId;
        packet.header.sequenceNum = i;
        packet.header.totalPackets = totalPackets;
        packet.header.timestamp = GetTimestampMicroseconds();

        size_t offset = i * MAX_UDP_PAYLOAD;
        size_t chunkSize = std::min(MAX_UDP_PAYLOAD, totalSize - offset);
        packet.header.dataSize = chunkSize;
        memcpy(packet.data, bitstream.data() + offset, chunkSize);

        if (i == totalPackets - 1) {
            packet.header.flags |= FLAG_LAST_PACKET;
        }

        // Send UDP packet
        sendto(udpSocket, (char*)&packet, sizeof(VideoPacketHeader) + chunkSize, 0,
               (sockaddr*)&clientAddr, sizeof(clientAddr));
    }
}
```

#### 4.3 Packet Reception (Client)

```cpp
// client/network_client.cpp
class PacketReceiver {
private:
    struct FrameBuffer {
        uint64_t frameId;
        uint32_t totalPackets;
        uint32_t receivedPackets;
        std::map<uint32_t, std::vector<uint8_t>> packets;
        uint64_t firstPacketTime;
        bool isKeyframe;
    };

    std::map<uint64_t, FrameBuffer> frameBuffers;

public:
    void ReceivePacket() {
        VideoPacket packet;
        int bytesReceived = recvfrom(udpSocket, (char*)&packet, sizeof(packet), 0, nullptr, nullptr);

        if (packet.header.magic != GUPT_MAGIC) {
            // Invalid packet
            return;
        }

        uint64_t frameId = packet.header.frameId;
        auto& frame = frameBuffers[frameId];

        if (frame.totalPackets == 0) {
            // First packet for this frame
            frame.frameId = frameId;
            frame.totalPackets = packet.header.totalPackets;
            frame.receivedPackets = 0;
            frame.firstPacketTime = GetTimestampMicroseconds();
            frame.isKeyframe = (packet.header.type == PACKET_TYPE_KEYFRAME);
        }

        // Store packet data
        frame.packets[packet.header.sequenceNum].assign(
            packet.data,
            packet.data + packet.header.dataSize
        );
        frame.receivedPackets++;

        // Check if frame complete
        if (frame.receivedPackets == frame.totalPackets) {
            ReassembleAndDecode(frame);
            frameBuffers.erase(frameId);
        }

        // Timeout old frames (>100ms)
        CleanupStaleFrames();
    }

    void ReassembleAndDecode(FrameBuffer& frame) {
        std::vector<uint8_t> completeFrame;
        for (uint32_t i = 0; i < frame.totalPackets; i++) {
            auto& chunk = frame.packets[i];
            completeFrame.insert(completeFrame.end(), chunk.begin(), chunk.end());
        }

        // Pass to decoder
        decoderQueue.Push(completeFrame);
    }
};
```

#### 4.4 Jitter Buffer

```cpp
// Simple jitter buffer: hold frames for up to 50ms
class JitterBuffer {
    std::deque<std::vector<uint8_t>> buffer;
    uint64_t lastDequeueTime;
    const uint64_t BUFFER_DELAY_US = 50000; // 50ms

public:
    void Enqueue(std::vector<uint8_t> frame) {
        buffer.push_back(std::move(frame));
    }

    bool Dequeue(std::vector<uint8_t>& frame) {
        if (buffer.empty()) return false;

        uint64_t now = GetTimestampMicroseconds();
        if (now - lastDequeueTime < BUFFER_DELAY_US) {
            return false; // Not yet
        }

        frame = std::move(buffer.front());
        buffer.pop_front();
        lastDequeueTime = now;
        return true;
    }
};
```

**Test Plan:**
- Send 100 frames from host
- Verify client receives and reassembles all
- Simulate packet loss (drop random packets)
- Verify frame timeout and cleanup
- Measure jitter (should be <20ms)

**Deliverable:** Reliable frame streaming over UDP

---

## Phase 5: Video Decoding & Rendering (Days 14-16)

### Objectives
- Decode H.264 stream on client
- Render to window using D3D11

### Tasks

#### 5.1 FFmpeg Decoder Setup

**File:** `client/decoder.h/.cpp`

```cpp
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>

class VideoDecoder {
private:
    AVCodecContext* codecCtx;
    AVCodecParserContext* parser;
    AVFrame* frame;
    AVPacket* pkt;

public:
    bool Initialize() {
        AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        codecCtx = avcodec_alloc_context3(codec);
        parser = av_parser_init(AV_CODEC_ID_H264);

        codecCtx->thread_count = 4;
        codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;

        avcodec_open2(codecCtx, codec, nullptr);

        frame = av_frame_alloc();
        pkt = av_packet_alloc();
        return true;
    }

    bool DecodeFrame(const uint8_t* data, size_t size, ID3D11Texture2D** outTexture) {
        int ret = av_parser_parse2(parser, codecCtx, &pkt->data, &pkt->size,
                                    data, size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

        if (pkt->size == 0) return false;

        ret = avcodec_send_packet(codecCtx, pkt);
        if (ret < 0) return false;

        ret = avcodec_receive_frame(codecCtx, frame);
        if (ret < 0) return false;

        // Convert YUV to RGBA and upload to D3D11 texture
        ConvertAndUpload(frame, outTexture);
        return true;
    }

    void ConvertAndUpload(AVFrame* frame, ID3D11Texture2D** outTexture) {
        // Use swscale to convert YUV420P -> RGBA
        // Then create D3D11 texture and copy
        // (Implementation details...)
    }
};
```

#### 5.2 D3D11 Renderer

**File:** `client/renderer.h/.cpp`

```cpp
class D3DRenderer {
private:
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    IDXGISwapChain* swapChain;
    ID3D11RenderTargetView* renderTargetView;

    ID3D11VertexShader* vertexShader;
    ID3D11PixelShader* pixelShader;
    ID3D11SamplerState* samplerState;

public:
    bool Initialize(HWND hwnd, uint32_t width, uint32_t height) {
        // Create device and swap chain
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = width;
        sd.BufferDesc.Height = height;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; // No VSync

        D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                       0, nullptr, 0, D3D11_SDK_VERSION,
                                       &sd, &swapChain, &device, nullptr, &context);

        // Create render target view
        ID3D11Texture2D* backBuffer;
        swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
        backBuffer->Release();

        // Load shaders (simple texture copy)
        CompileShaders();

        return true;
    }

    void RenderFrame(ID3D11Texture2D* videoTexture) {
        // Set render target
        context->OMSetRenderTargets(1, &renderTargetView, nullptr);

        // Clear
        float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        context->ClearRenderTargetView(renderTargetView, clearColor);

        // Create shader resource view from video texture
        ID3D11ShaderResourceView* srv;
        device->CreateShaderResourceView(videoTexture, nullptr, &srv);

        // Render fullscreen quad with video texture
        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->PSSetShaderResources(0, 1, &srv);
        context->PSSetSamplers(0, 1, &samplerState);
        context->Draw(6, 0); // Two triangles

        // Present (no VSync)
        swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);

        srv->Release();
    }
};
```

**Test Plan:**
- Decode pre-recorded H.264 file
- Display decoded frames in window
- Verify frame rate (should be 30-60 FPS)
- Measure decode time (<8ms target)
- Test with different resolutions

**Deliverable:** Client displaying remote desktop video

---

## Phase 6: Input Control (Days 17-19)

### Objectives
- Capture input on client
- Send to host via TCP
- Inject input on host

### Tasks

#### 6.1 Input Capture (Client)

**File:** `client/input_sender.h/.cpp`

```cpp
class InputSender {
private:
    HWND targetWindow;
    SOCKET tcpSocket;

public:
    void SetupHooks(HWND hwnd) {
        targetWindow = hwnd;
        // Window procedures will call our handlers
    }

    void OnMouseMove(int x, int y) {
        InputPacket pkt = {};
        pkt.type = INPUT_MOUSE_MOVE;
        pkt.x = x;
        pkt.y = y;
        pkt.timestamp = GetTimestampMicroseconds();

        send(tcpSocket, (char*)&pkt, sizeof(pkt), 0);
    }

    void OnMouseButton(uint16_t button, bool down) {
        InputPacket pkt = {};
        pkt.type = INPUT_MOUSE_BUTTON;
        pkt.flags = (button << 8) | (down ? 1 : 0);
        pkt.timestamp = GetTimestampMicroseconds();

        send(tcpSocket, (char*)&pkt, sizeof(pkt), 0);
    }

    void OnKeyboard(uint32_t vkCode, bool down) {
        InputPacket pkt = {};
        pkt.type = INPUT_KEYBOARD;
        pkt.vkCode = vkCode;
        pkt.flags = down ? 1 : 0;
        pkt.timestamp = GetTimestampMicroseconds();

        send(tcpSocket, (char*)&pkt, sizeof(pkt), 0);
    }
};
```

#### 6.2 Window Procedure

```cpp
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static InputSender* inputSender = nullptr;

    switch (msg) {
        case WM_CREATE:
            inputSender = (InputSender*)((CREATESTRUCT*)lParam)->lpCreateParams;
            break;

        case WM_MOUSEMOVE:
            if (inputSender) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                inputSender->OnMouseMove(x, y);
            }
            break;

        case WM_LBUTTONDOWN:
            inputSender->OnMouseButton(0, true);
            break;

        case WM_LBUTTONUP:
            inputSender->OnMouseButton(0, false);
            break;

        case WM_KEYDOWN:
            inputSender->OnKeyboard(wParam, true);
            break;

        case WM_KEYUP:
            inputSender->OnKeyboard(wParam, false);
            break;

        // ... more input handling ...
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
```

#### 6.3 Input Injection (Host)

**File:** `host/input_handler.h/.cpp`

```cpp
class InputHandler {
private:
    uint32_t screenWidth, screenHeight;

public:
    void Initialize() {
        screenWidth = GetSystemMetrics(SM_CXSCREEN);
        screenHeight = GetSystemMetrics(SM_CYSCREEN);
    }

    void InjectMouseMove(int x, int y) {
        // Convert to absolute coordinates
        int absX = (x * 65536) / screenWidth;
        int absY = (y * 65536) / screenHeight;

        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dx = absX;
        input.mi.dy = absY;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

        SendInput(1, &input, sizeof(INPUT));
    }

    void InjectMouseButton(uint16_t button, bool down) {
        INPUT input = {};
        input.type = INPUT_MOUSE;

        switch (button) {
            case 0: // Left
                input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
                break;
            case 1: // Right
                input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
                break;
            case 2: // Middle
                input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
                break;
        }

        SendInput(1, &input, sizeof(INPUT));
    }

    void InjectKeyboard(uint32_t vkCode, bool down) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vkCode;
        input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;

        SendInput(1, &input, sizeof(INPUT));
    }

    void ProcessInputPacket(const InputPacket& pkt) {
        switch (pkt.type) {
            case INPUT_MOUSE_MOVE:
                InjectMouseMove(pkt.x, pkt.y);
                break;
            case INPUT_MOUSE_BUTTON:
                InjectMouseButton((pkt.flags >> 8) & 0xFF, pkt.flags & 1);
                break;
            case INPUT_KEYBOARD:
                InjectKeyboard(pkt.vkCode, pkt.flags & 1);
                break;
        }
    }
};
```

**Test Plan:**
- Move mouse in client window, verify cursor moves on host
- Click in client, verify click registered on host
- Type in client, verify text appears on host
- Measure input latency (<20ms target)

**Deliverable:** Full input control from client to host

---

## Phase 7: Signaling Server (Days 20-22)

### Objectives
- Implement Go-based signaling server
- Client/host registration
- Peer discovery and NAT traversal

### Tasks

#### 7.1 Signaling Server (Go)

**File:** `signaling/main.go`

```go
package main

import (
    "encoding/json"
    "net/http"
    "sync"
    "time"
)

type Session struct {
    SessionID    string    `json:"session_id"`
    PublicIP     string    `json:"public_ip"`
    Port         uint16    `json:"port"`
    PasswordHash string    `json:"password_hash"`
    CreatedAt    time.Time `json:"created_at"`
}

var (
    sessions = make(map[string]*Session)
    mu       sync.RWMutex
)

func handleRegister(w http.ResponseWriter, r *http.Request) {
    var req struct {
        Type         string `json:"type"`
        SessionID    string `json:"session_id"`
        Port         uint16 `json:"port"`
        PasswordHash string `json:"password_hash"`
    }

    json.NewDecoder(r.Body).Decode(&req)

    // Get client's public IP
    publicIP := r.RemoteAddr // Extract IP

    mu.Lock()
    sessions[req.SessionID] = &Session{
        SessionID:    req.SessionID,
        PublicIP:     publicIP,
        Port:         req.Port,
        PasswordHash: req.PasswordHash,
        CreatedAt:    time.Now(),
    }
    mu.Unlock()

    resp := map[string]interface{}{
        "type":       "registered",
        "session_id": req.SessionID,
        "public_ip":  publicIP,
        "expires_in": 3600,
    }

    json.NewEncoder(w).Encode(resp)
}

func handleConnect(w http.ResponseWriter, r *http.Request) {
    var req struct {
        Type         string `json:"type"`
        SessionID    string `json:"session_id"`
        PasswordHash string `json:"password_hash"`
    }

    json.NewDecoder(r.Body).Decode(&req)

    mu.RLock()
    session, exists := sessions[req.SessionID]
    mu.RUnlock()

    if !exists {
        http.Error(w, "Session not found", http.StatusNotFound)
        return
    }

    // Verify password
    if session.PasswordHash != req.PasswordHash {
        http.Error(w, "Unauthorized", http.StatusUnauthorized)
        return
    }

    resp := map[string]interface{}{
        "type":          "peer_info",
        "host_ip":       session.PublicIP,
        "host_port":     session.Port,
        "session_valid": true,
    }

    json.NewEncoder(w).Encode(resp)
}

func main() {
    http.HandleFunc("/register", handleRegister)
    http.HandleFunc("/connect", handleConnect)

    http.ListenAndServe(":8080", nil)
}
```

#### 7.2 Host Registration

**File:** `host/signaling_client.cpp`

```cpp
class SignalingClient {
public:
    bool RegisterSession(const std::string& signalingServer,
                         const std::string& sessionId,
                         const std::string& password,
                         uint16_t port) {
        // Create JSON payload
        nlohmann::json payload;
        payload["type"] = "register";
        payload["session_id"] = sessionId;
        payload["port"] = port;
        payload["password_hash"] = SHA256(password);

        // HTTP POST to signaling server
        std::string response = HttpPost(signalingServer + "/register", payload.dump());

        // Parse response
        auto resp = nlohmann::json::parse(response);
        if (resp["type"] == "registered") {
            std::cout << "Registered with session ID: " << sessionId << "\n";
            std::cout << "Public IP: " << resp["public_ip"] << "\n";
            return true;
        }

        return false;
    }
};
```

#### 7.3 Client Connection

```cpp
// client/signaling_client.cpp
bool SignalingClient::ConnectViaSignaling(const std::string& signalingServer,
                                          const std::string& sessionId,
                                          const std::string& password,
                                          std::string& outHostIP,
                                          uint16_t& outHostPort) {
    nlohmann::json payload;
    payload["type"] = "connect";
    payload["session_id"] = sessionId;
    payload["password_hash"] = SHA256(password);

    std::string response = HttpPost(signalingServer + "/connect", payload.dump());

    auto resp = nlohmann::json::parse(response);
    if (resp["type"] == "peer_info" && resp["session_valid"] == true) {
        outHostIP = resp["host_ip"];
        outHostPort = resp["host_port"];
        return true;
    }

    return false;
}
```

**Test Plan:**
- Run signaling server on port 8080
- Host registers with session ID "test123"
- Client connects using "test123"
- Verify peer info returned correctly
- Test with wrong password (should fail)

**Deliverable:** Working signaling server for P2P connection

---

## Phase 8: Integration & Polish (Days 23-25)

### Objectives
- Integrate all components
- Add error handling and reconnection
- Performance tuning

### Tasks

#### 8.1 Full Pipeline Integration

**Host main.cpp:**
```cpp
int main() {
    // Initialize components
    ScreenCapture capture;
    NvencEncoder encoder;
    NetworkServer server;
    InputHandler inputHandler;

    capture.Initialize();
    encoder.Initialize(1920, 1080, 5000000);
    server.Initialize(5900);
    inputHandler.Initialize();

    // Start threads
    std::thread captureThread([&]() {
        while (running) {
            ID3D11Texture2D* frame;
            bool available;

            if (capture.CaptureFrame(&frame, available) && available) {
                captureQueue.Push(frame);
            }
            capture.ReleaseFrame();
        }
    });

    std::thread encodeThread([&]() {
        while (running) {
            ID3D11Texture2D* frame;
            if (captureQueue.Pop(frame, 100)) {
                std::vector<uint8_t> bitstream;
                encoder.EncodeFrame(frame, bitstream);
                encoderQueue.Push(bitstream);
            }
        }
    });

    std::thread networkThread([&]() {
        while (running) {
            std::vector<uint8_t> bitstream;
            if (encoderQueue.Pop(bitstream, 100)) {
                server.SendEncodedFrame(bitstream, frameId++, false);
            }
        }
    });

    std::thread inputThread([&]() {
        while (running) {
            InputPacket pkt;
            if (server.ReceiveInputPacket(pkt)) {
                inputHandler.ProcessInputPacket(pkt);
            }
        }
    });

    // Wait
    captureThread.join();
    encodeThread.join();
    networkThread.join();
    inputThread.join();

    return 0;
}
```

#### 8.2 Reconnection Logic

```cpp
class ConnectionManager {
    bool autoReconnect = true;
    int reconnectAttempts = 0;
    const int MAX_RECONNECT_ATTEMPTS = 10;

public:
    void HandleDisconnect() {
        if (!autoReconnect) return;

        while (reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
            std::cout << "Attempting reconnect " << (reconnectAttempts + 1) << "...\n";

            if (client.Connect(lastIP, lastPort)) {
                std::cout << "Reconnected successfully!\n";
                reconnectAttempts = 0;
                return;
            }

            reconnectAttempts++;
            Sleep(1000 * reconnectAttempts); // Exponential backoff
        }

        std::cout << "Failed to reconnect after " << MAX_RECONNECT_ATTEMPTS << " attempts\n";
    }
};
```

#### 8.3 Performance Monitoring

```cpp
class PerformanceMonitor {
    struct Metrics {
        uint64_t captureTime;
        uint64_t encodeTime;
        uint64_t networkTime;
        uint64_t decodeTime;
        uint64_t renderTime;
        uint64_t totalLatency;
        uint32_t packetLoss;
        uint32_t fps;
    };

public:
    void LogMetrics() {
        std::cout << "=== Performance Metrics ===\n";
        std::cout << "Capture:  " << metrics.captureTime << " us\n";
        std::cout << "Encode:   " << metrics.encodeTime << " us\n";
        std::cout << "Network:  " << metrics.networkTime << " us\n";
        std::cout << "Decode:   " << metrics.decodeTime << " us\n";
        std::cout << "Render:   " << metrics.renderTime << " us\n";
        std::cout << "Total:    " << metrics.totalLatency << " us\n";
        std::cout << "Loss:     " << metrics.packetLoss << "%\n";
        std::cout << "FPS:      " << metrics.fps << "\n";
    }
};
```

**Test Plan:**
- Full end-to-end test (capture → encode → stream → decode → render → input)
- Measure latency (should be <100ms)
- Test on LAN
- Test over internet via signaling
- Simulate network issues (packet loss, high latency)
- Profile with NVIDIA Nsight

**Deliverable:** Complete, production-ready remote desktop application

---

## Build Instructions

### Prerequisites
1. Visual Studio 2019 or later
2. Windows SDK 10.0.19041+
3. NVIDIA Video Codec SDK 12.x
4. CUDA Toolkit 11.8+
5. FFmpeg 5.x (for fallback/decoding)
6. CMake 3.20+

### Setup Steps
```bash
# Clone repository
git clone https://github.com/yourrepo/gupt.git
cd gupt

# Extract NVIDIA Video Codec SDK
# Download from: https://developer.nvidia.com/nvidia-video-codec-sdk
# Extract to: third_party/nvenc/

# Build FFmpeg (optional, for software fallback)
# Or download pre-built binaries

# Generate Visual Studio solution
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" -A x64

# Open in Visual Studio
start GuPT.sln

# Build
# Ctrl+Shift+B or Build -> Build Solution
```

### CMakeLists.txt Configuration
```cmake
cmake_minimum_required(VERSION 3.20)
project(GuPT)

set(CMAKE_CXX_STANDARD 17)

# Find DirectX
find_package(DirectX REQUIRED)

# NVENC paths
set(NVENC_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/nvenc/include")

# Host executable
add_executable(gupt_host
    host/main.cpp
    host/capture.cpp
    host/encoder.cpp
    host/network_server.cpp
    host/input_handler.cpp
    common/utils.cpp
    common/logger.cpp
)

target_include_directories(gupt_host PRIVATE
    ${NVENC_INCLUDE_DIR}
    ${DirectX_INCLUDE_DIRS}
)

target_link_libraries(gupt_host
    d3d11.lib
    dxgi.lib
    ws2_32.lib
)

# Client executable
add_executable(gupt_client
    client/main.cpp
    client/decoder.cpp
    client/renderer.cpp
    client/network_client.cpp
    client/input_sender.cpp
    common/utils.cpp
    common/logger.cpp
)

# Similar configuration...
```

---

## Testing Checklist

### Unit Tests
- [ ] Packet serialization/deserialization
- [ ] Queue push/pop operations
- [ ] Coordinate transformations

### Integration Tests
- [ ] TCP connection establishment
- [ ] UDP packet transmission
- [ ] DXGI frame capture
- [ ] NVENC encoding
- [ ] FFmpeg decoding
- [ ] D3D11 rendering
- [ ] Input injection

### End-to-End Tests
- [ ] Loopback (127.0.0.1)
- [ ] LAN connection
- [ ] Internet connection via signaling
- [ ] Reconnection after disconnect
- [ ] Resolution change handling
- [ ] Multi-monitor support

### Performance Tests
- [ ] Latency <100ms
- [ ] FPS 30-60
- [ ] Packet loss <1%
- [ ] CPU usage <30%
- [ ] GPU usage efficient

---

## Timeline Summary

| Phase | Duration | Milestone |
|-------|----------|-----------|
| 1. Foundation | 3 days | TCP/UDP working |
| 2. Capture | 3 days | Screen capture working |
| 3. Encoding | 4 days | NVENC encoding working |
| 4. Streaming | 3 days | UDP video streaming |
| 5. Decoding | 3 days | Video rendering on client |
| 6. Input | 3 days | Input control working |
| 7. Signaling | 3 days | P2P connection via server |
| 8. Polish | 3 days | Production ready |
| **Total** | **25 days** | **Complete system** |

---

## Next Steps

We'll start with **Phase 1: Foundation & Networking**. I'll create:

1. Project structure (directories, CMakeLists.txt)
2. Common headers (protocol.h, packet.h)
3. Basic TCP server/client
4. Basic UDP socket setup
5. Test programs

Ready to begin implementation?
