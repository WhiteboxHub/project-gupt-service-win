# GuPT Codec Integration Guide

This document provides detailed instructions for integrating real video codecs (NVENC for encoding, FFmpeg for decoding) into the GuPT remote desktop system.

## Overview

The current implementation uses stub encoders and decoders for testing the pipeline. This guide explains how to replace them with production-ready codecs.

## Table of Contents

1. [NVENC Integration (Host Side)](#nvenc-integration-host-side)
2. [FFmpeg Integration (Client Side)](#ffmpeg-integration-client-side)
3. [Performance Tuning](#performance-tuning)
4. [Troubleshooting](#troubleshooting)

---

## NVENC Integration (Host Side)

### Prerequisites

1. **NVIDIA GPU**: GTX 900 series or newer
2. **NVIDIA Video Codec SDK**: Download from [NVIDIA Developer](https://developer.nvidia.com/nvidia-video-codec-sdk)
3. **CUDA Toolkit**: Version 11.0 or newer
4. **Latest NVIDIA Drivers**: 4 70.00 or newer

### Installation Steps

**1. Download NVIDIA Video Codec SDK**

```bash
# Download from: https://developer.nvidia.com/nvidia-video-codec-sdk
# Extract to: GuPT/third_party/nvenc/
```

**2. Update CMakeLists.txt**

```cmake
# Add NVIDIA Video Codec SDK paths
include_directories(${CMAKE_SOURCE_DIR}/third_party/nvenc/include)
link_directories(${CMAKE_SOURCE_DIR}/third_party/nvenc/lib/x64)

# Add CUDA paths
find_package(CUDA REQUIRED)
include_directories(${CUDA_INCLUDE_DIRS})

# Link NVENC libraries
target_link_libraries(gupt_host PRIVATE
    gupt_common
    d3d11.lib
    dxgi.lib
    ws2_32.lib
    cuda.lib
    cudart.lib
    nvcuvid.lib  # For decoder (optional)
)
```

**3. Implement NVENCEncoder**

The stub encoder in `host/encoder.cpp` needs to be replaced with a real NVENC implementation:

```cpp
// host/encoder.cpp - NVENC Implementation

#include "encoder.h"
#include "../common/logger.h"
#include <nvEncodeAPI.h>
#include <d3d11.h>
#include <cuda.h>
#include <cudaD3D11.h>

class NVENCEncoder : public VideoEncoder {
public:
    NVENCEncoder() : encoder(nullptr), cudaContext(nullptr) {}
    ~NVENCEncoder() { Shutdown(); }

    bool Initialize(ID3D11Device* device, const EncoderConfig& config) override {
        if (!device) {
            LOG_ERROR("Invalid D3D11 device");
            return false;
        }

        this->device = device;
        this->config = config;

        // Initialize CUDA-D3D11 interop
        if (!InitializeCUDA()) {
            LOG_ERROR("Failed to initialize CUDA");
            return false;
        }

        // Initialize NVENC
        if (!InitializeNVENC()) {
            LOG_ERROR("Failed to initialize NVENC");
            return false;
        }

        LOG_INFO("NVENC encoder initialized successfully");
        return true;
    }

    bool EncodeFrame(ID3D11Texture2D* texture, EncodedFrame& outFrame) override {
        if (!encoder || !texture) {
            return false;
        }

        // Map D3D11 texture to CUDA
        CUgraphicsResource cudaResource;
        if (!MapD3D11ToCUDA(texture, cudaResource)) {
            return false;
        }

        // Get CUDA array
        CUarray cudaArray;
        cuGraphicsSubResourceGetMappedArray(&cudaArray, cudaResource, 0, 0);

        // Encode with NVENC
        NV_ENC_PIC_PARAMS picParams = {};
        picParams.version = NV_ENC_PIC_PARAMS_VER;
        picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        picParams.inputBuffer = (void*)cudaArray;
        picParams.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
        picParams.inputWidth = config.width;
        picParams.inputHeight = config.height;
        picParams.outputBitstream = outputBitstreamBuffer;

        // Submit encode
        NVENCSTATUS status = nvEncEncodePicture(encoder, &picParams);
        if (status != NV_ENC_SUCCESS) {
            LOG_ERROR_FMT("NVENC encode failed: %d", status);
            UnmapCUDA(cudaResource);
            return false;
        }

        // Lock output bitstream
        NV_ENC_LOCK_BITSTREAM lockParams = {};
        lockParams.version = NV_ENC_LOCK_BITSTREAM_VER;
        lockParams.outputBitstream = outputBitstreamBuffer;

        status = nvEncLockBitstream(encoder, &lockParams);
        if (status != NV_ENC_SUCCESS) {
            UnmapCUDA(cudaResource);
            return false;
        }

        // Copy bitstream to output
        outFrame.bitstream = new uint8_t[lockParams.bitstreamSizeInBytes];
        outFrame.bitstreamSize = lockParams.bitstreamSizeInBytes;
        memcpy(outFrame.bitstream, lockParams.bitstreamBufferPtr, lockParams.bitstreamSizeInBytes);
        outFrame.frameNumber = frameCounter++;
        outFrame.timestamp = PerformanceTimer::GetTimestampMicroseconds();
        outFrame.isKeyframe = (lockParams.pictureType == NV_ENC_PIC_TYPE_IDR);

        // Unlock bitstream
        nvEncUnlockBitstream(encoder, outputBitstreamBuffer);
        UnmapCUDA(cudaResource);

        return true;
    }

    void Shutdown() override {
        if (encoder) {
            nvEncDestroyEncoder(encoder);
            encoder = nullptr;
        }

        if (cudaContext) {
            cuCtxDestroy(cudaContext);
            cudaContext = nullptr;
        }

        LOG_INFO("NVENC encoder shut down");
    }

    EncoderType GetType() const override { return EncoderType::NVENC; }

private:
    bool InitializeCUDA() {
        // Initialize CUDA
        if (cuInit(0) != CUDA_SUCCESS) {
            return false;
        }

        // Get CUDA device from D3D11 device
        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;

        if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice))) {
            return false;
        }

        if (FAILED(dxgiDevice->GetAdapter(&adapter))) {
            dxgiDevice->Release();
            return false;
        }

        // Create CUDA context
        CUdevice cudaDevice;
        unsigned int deviceCount;
        cuD3D11GetDevices(&deviceCount, &cudaDevice, 1, adapter, CU_D3D11_DEVICE_LIST_ALL);

        if (cuCtxCreate(&cudaContext, 0, cudaDevice) != CUDA_SUCCESS) {
            adapter->Release();
            dxgiDevice->Release();
            return false;
        }

        adapter->Release();
        dxgiDevice->Release();

        return true;
    }

    bool InitializeNVENC() {
        // Load NVENC API
        NV_ENCODE_API_FUNCTION_LIST nvenc = { NV_ENCODE_API_FUNCTION_LIST_VER };
        NVENCSTATUS status = NvEncodeAPICreateInstance(&nvenc);
        if (status != NV_ENC_SUCCESS) {
            return false;
        }

        // Open encode session
        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sessionParams = {};
        sessionParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        sessionParams.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
        sessionParams.device = cudaContext;
        sessionParams.apiVersion = NVENCAPI_VERSION;

        status = nvenc.nvEncOpenEncodeSessionEx(&sessionParams, &encoder);
        if (status != NV_ENC_SUCCESS) {
            return false;
        }

        // Initialize encoder
        NV_ENC_INITIALIZE_PARAMS initParams = {};
        initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
        initParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
        initParams.presetGUID = NV_ENC_PRESET_LOW_LATENCY_HQ_GUID;
        initParams.encodeWidth = config.width;
        initParams.encodeHeight = config.height;
        initParams.darWidth = config.width;
        initParams.darHeight = config.height;
        initParams.frameRateNum = config.frameRate;
        initParams.frameRateDen = 1;
        initParams.enablePTD = 1;

        // Configure codec
        NV_ENC_CONFIG encConfig = {};
        encConfig.version = NV_ENC_CONFIG_VER;
        encConfig.profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
        encConfig.gopLength = NVENC_INFINITE_GOPLENGTH;
        encConfig.frameIntervalP = 1;
        encConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
        encConfig.rcParams.averageBitRate = config.bitrate;
        encConfig.rcParams.maxBitRate = config.bitrate;
        encConfig.rcParams.vbvBufferSize = config.bitrate / config.frameRate;
        encConfig.rcParams.vbvInitialDelay = encConfig.rcParams.vbvBufferSize;

        initParams.encodeConfig = &encConfig;

        status = nvEncInitializeEncoder(encoder, &initParams);
        if (status != NV_ENC_SUCCESS) {
            return false;
        }

        // Allocate output bitstream buffer
        NV_ENC_CREATE_BITSTREAM_BUFFER createBitstreamParams = {};
        createBitstreamParams.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
        status = nvEncCreateBitstreamBuffer(encoder, &createBitstreamParams);
        if (status != NV_ENC_SUCCESS) {
            return false;
        }

        outputBitstreamBuffer = createBitstreamParams.bitstreamBuffer;

        return true;
    }

    bool MapD3D11ToCUDA(ID3D11Texture2D* texture, CUgraphicsResource& resource) {
        CUresult result = cuGraphicsD3D11RegisterResource(
            &resource,
            texture,
            CU_GRAPHICS_REGISTER_FLAGS_NONE
        );

        if (result != CUDA_SUCCESS) {
            return false;
        }

        result = cuGraphicsMapResources(1, &resource, 0);
        return result == CUDA_SUCCESS;
    }

    void UnmapCUDA(CUgraphicsResource resource) {
        cuGraphicsUnmapResources(1, &resource, 0);
        cuGraphicsUnregisterResource(resource);
    }

private:
    ID3D11Device* device = nullptr;
    EncoderConfig config;
    void* encoder;
    CUcontext cudaContext;
    void* outputBitstreamBuffer;
    uint64_t frameCounter = 0;
};
```

**4. Update EncoderFactory**

```cpp
std::unique_ptr<VideoEncoder> EncoderFactory::CreateEncoder(
    ID3D11Device* device,
    const EncoderConfig& config)
{
    // Try NVENC first
    auto nvencEncoder = std::make_unique<NVENCEncoder>();
    if (nvencEncoder->Initialize(device, config)) {
        LOG_INFO("Using NVENC hardware encoder");
        return nvencEncoder;
    }

    LOG_WARNING("NVENC not available, using software encoder");

    // Fallback to software encoder
    auto softwareEncoder = std::make_unique<SoftwareEncoder>();
    if (softwareEncoder->Initialize(device, config)) {
        return softwareEncoder;
    }

    LOG_ERROR("Failed to create any encoder");
    return nullptr;
}
```

**5. Build and Test**

```bash
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DUSE_NVENC=ON
cmake --build . --config Release

# Run and verify NVENC is being used
bin/Release/gupt_host.exe
# Look for: "Using NVENC hardware encoder"
```

### NVENC Performance Tips

1. **Low Latency Preset**: Use `NV_ENC_PRESET_LOW_LATENCY_HQ_GUID` for minimal latency
2. **CBR Rate Control**: Use `NV_ENC_PARAMS_RC_CBR` for consistent bitrate
3. **Infinite GOP**: Set `gopLength = NVENC_INFINITE_GOPLENGTH` for best latency
4. **Async Encoding**: Use async mode for better throughput
5. **Multiple Sessions**: NVENC supports multiple encoding sessions

### Common NVENC Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `NV_ENC_ERR_NO_ENCODE_DEVICE` | No NVIDIA GPU | Check GPU, update drivers |
| `NV_ENC_ERR_UNSUPPORTED_DEVICE` | GPU too old | GTX 900+ required |
| `NV_ENC_ERR_OUT_OF_MEMORY` | GPU memory full | Reduce resolution/bitrate |
| `NV_ENC_ERR_ENCODER_BUSY` | Too many sessions | Limit concurrent encoders |

---

## FFmpeg Integration (Client Side)

### Prerequisites

1. **FFmpeg Libraries**: Download from [FFmpeg.org](https://ffmpeg.org/download.html)
2. **Visual Studio**: 2019 or newer
3. **FFmpeg DLLs**: avcodec, avutil, avformat, swscale

### Installation Steps

**1. Download FFmpeg**

```bash
# Download Windows build: https://www.gyan.dev/ffmpeg/builds/
# Extract to: GuPT/third_party/ffmpeg/
```

**2. Update CMakeLists.txt**

```cmake
# Add FFmpeg paths
include_directories(${CMAKE_SOURCE_DIR}/third_party/ffmpeg/include)
link_directories(${CMAKE_SOURCE_DIR}/third_party/ffmpeg/lib)

# Link FFmpeg libraries
target_link_libraries(gupt_client PRIVATE
    gupt_common
    d3d11.lib
    dxgi.lib
    ws2_32.lib
    avcodec.lib
    avutil.lib
    avformat.lib
    swscale.lib
)

# Copy FFmpeg DLLs to output directory
add_custom_command(TARGET gupt_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/bin"
    "$<TARGET_FILE_DIR:gupt_client>"
)
```

**3. Implement FFmpegDecoder**

Replace the stub decoder in `client/decoder.cpp`:

```cpp
// client/decoder.cpp - FFmpeg Implementation

#include "decoder.h"
#include "../common/logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

class SoftwareDecoder : public VideoDecoder {
public:
    SoftwareDecoder() : codec(nullptr), codecContext(nullptr), frame(nullptr), packet(nullptr) {}
    ~SoftwareDecoder() { Shutdown(); }

    bool Initialize(ID3D11Device* device, const DecoderConfig& config) override {
        this->device = device;
        this->config = config;

        // Find H.264 decoder
        codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (!codec) {
            LOG_ERROR("H.264 decoder not found");
            return false;
        }

        // Allocate codec context
        codecContext = avcodec_alloc_context3(codec);
        if (!codecContext) {
            LOG_ERROR("Failed to allocate codec context");
            return false;
        }

        // Configure decoder
        codecContext->width = config.width;
        codecContext->height = config.height;
        codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
        codecContext->thread_count = 4;  // Multi-threaded decoding
        codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
        codecContext->flags2 |= AV_CODEC_FLAG2_FAST;

        // Open codec
        if (avcodec_open2(codecContext, codec, nullptr) < 0) {
            LOG_ERROR("Failed to open codec");
            avcodec_free_context(&codecContext);
            return false;
        }

        // Allocate frame and packet
        frame = av_frame_alloc();
        packet = av_packet_alloc();

        if (!frame || !packet) {
            LOG_ERROR("Failed to allocate frame/packet");
            Shutdown();
            return false;
        }

        // Create D3D11 texture for output
        if (!CreateOutputTexture()) {
            LOG_ERROR("Failed to create output texture");
            Shutdown();
            return false;
        }

        // Initialize swscale context for YUV->RGBA conversion
        swsContext = sws_getContext(
            config.width, config.height, AV_PIX_FMT_YUV420P,
            config.width, config.height, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        if (!swsContext) {
            LOG_ERROR("Failed to create swscale context");
            Shutdown();
            return false;
        }

        LOG_INFO("FFmpeg decoder initialized successfully");
        return true;
    }

    bool DecodeFrame(const ReceivedFrame& receivedFrame, DecodedFrame& outFrame) override {
        if (!codecContext || !frame || !packet) {
            return false;
        }

        // Fill packet with received data
        packet->data = const_cast<uint8_t*>(receivedFrame.data.data());
        packet->size = static_cast<int>(receivedFrame.data.size());

        // Send packet to decoder
        int ret = avcodec_send_packet(codecContext, packet);
        if (ret < 0) {
            LOG_ERROR_FMT("Error sending packet to decoder: %d", ret);
            return false;
        }

        // Receive decoded frame
        ret = avcodec_receive_frame(codecContext, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return false;  // Need more data
        } else if (ret < 0) {
            LOG_ERROR_FMT("Error during decoding: %d", ret);
            return false;
        }

        // Convert YUV420 to RGBA
        uint8_t* destData[4] = { stagingBuffer, nullptr, nullptr, nullptr };
        int destLinesize[4] = { config.width * 4, 0, 0, 0 };

        sws_scale(
            swsContext,
            frame->data, frame->linesize,
            0, config.height,
            destData, destLinesize
        );

        // Copy to D3D11 texture
        CopyToTexture(stagingBuffer);

        // Fill output
        outFrame.texture = outputTexture.Get();
        outFrame.frameNumber = receivedFrame.frameNumber;
        outFrame.timestamp = receivedFrame.timestamp;
        outFrame.isKeyframe = receivedFrame.isKeyframe;

        // Unreference frame
        av_frame_unref(frame);

        return true;
    }

    void Shutdown() override {
        if (swsContext) {
            sws_freeContext(swsContext);
            swsContext = nullptr;
        }

        if (frame) {
            av_frame_free(&frame);
        }

        if (packet) {
            av_packet_free(&packet);
        }

        if (codecContext) {
            avcodec_free_context(&codecContext);
        }

        if (stagingBuffer) {
            delete[] stagingBuffer;
            stagingBuffer = nullptr;
        }

        outputTexture.Reset();

        LOG_INFO("FFmpeg decoder shut down");
    }

private:
    bool CreateOutputTexture() {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = config.width;
        desc.Height = config.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &outputTexture);
        if (FAILED(hr)) {
            return false;
        }

        // Allocate staging buffer
        stagingBuffer = new uint8_t[config.width * config.height * 4];

        return true;
    }

    void CopyToTexture(uint8_t* data) {
        ID3D11DeviceContext* context = nullptr;
        device->GetImmediateContext(&context);

        context->UpdateSubresource(
            outputTexture.Get(),
            0,
            nullptr,
            data,
            config.width * 4,
            0
        );

        context->Release();
    }

private:
    ID3D11Device* device = nullptr;
    DecoderConfig config;
    const AVCodec* codec;
    AVCodecContext* codecContext;
    AVFrame* frame;
    AVPacket* packet;
    SwsContext* swsContext = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> outputTexture;
    uint8_t* stagingBuffer = nullptr;
};
```

**4. Build and Test**

```bash
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DUSE_FFMPEG=ON
cmake --build . --config Release

# Ensure FFmpeg DLLs are in bin/Release/
# Run client
bin/Release/gupt_client.exe --host 127.0.0.1
```

### FFmpeg Performance Tips

1. **Thread Count**: Set `thread_count = 4` or higher
2. **Low Delay**: Use `AV_CODEC_FLAG_LOW_DELAY` flag
3. **Zero Copy**: Use D3D11VA for hardware decoding if available
4. **Frame Dropping**: Drop frames if decoder falls behind

### Common FFmpeg Errors

| Error | Cause | Solution |
|-------|-------|----------|
| DLL not found | FFmpeg DLLs missing | Copy DLLs to exe directory |
| Codec not found | Wrong codec ID | Use `AV_CODEC_ID_H264` |
| Invalid data | Corrupted packet | Check network, request keyframe |
| Memory leak | Frame not unref'd | Call `av_frame_unref()` |

---

## Performance Tuning

### Encoder Optimization

**NVENC Settings for Low Latency**:
```cpp
encConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
encConfig.rcParams.enableMinQP = 1;
encConfig.rcParams.minQP = {15, 15, 15};  // {P, B, I}
encConfig.rcParams.maxQP = {35, 35, 35};
encConfig.gopLength = NVENC_INFINITE_GOPLENGTH;
encConfig.frameIntervalP = 1;  // No B-frames
```

**Resolution and Bitrate Recommendations**:
| Resolution | Framerate | Bitrate (Mbps) | Use Case |
|------------|-----------|----------------|----------|
| 1920x1080 | 60 | 10-15 | Gaming, high motion |
| 1920x1080 | 30 | 5-8 | General desktop |
| 1280x720 | 60 | 5-8 | Low bandwidth gaming |
| 1280x720 | 30 | 2-4 | Low bandwidth desktop |

### Decoder Optimization

**FFmpeg Thread Settings**:
```cpp
codecContext->thread_count = std::thread::hardware_concurrency();
codecContext->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
```

**Hardware Acceleration** (Intel/AMD):
```cpp
// Use D3D11VA for hardware decoding
av_opt_set_int(codecContext, "hwaccel", AV_HWACCEL_D3D11VA, 0);
```

### Network Optimization

**UDP Socket Buffer Size**:
```cpp
int bufferSize = 2 * 1024 * 1024;  // 2 MB
setsockopt(udpSocket, SOL_SOCKET, SO_RCVBUF, (char*)&bufferSize, sizeof(bufferSize));
setsockopt(udpSocket, SOL_SOCKET, SO_SNDBUF, (char*)&bufferSize, sizeof(bufferSize));
```

**Jitter Buffer Tuning**:
```cpp
// Reduce jitter buffer for lower latency
config.jitterBufferMs = 20;  // Default is 50ms

// Increase for unstable networks
config.jitterBufferMs = 100;
```

---

## Troubleshooting

### Black Screen on Client

**Symptoms**: Client window shows black screen, no video

**Possible Causes**:
1. Decoder stub still in use
2. FFmpeg not linked properly
3. Corrupted video packets

**Solutions**:
```bash
# Check if FFmpeg DLLs are present
ls bin/Release/*.dll

# Check decoder logs
# Look for: "FFmpeg decoder initialized successfully"

# Request keyframe
# In network_client.cpp, send keyframe request
```

### High Latency

**Symptoms**: >100ms input-to-display latency

**Causes and Solutions**:

| Cause | Solution |
|-------|----------|
| Large jitter buffer | Reduce `jitterBufferMs` to 20-30ms |
| Encoder GOP too long | Set `gopLength = NVENC_INFINITE_GOPLENGTH` |
| B-frames enabled | Set `frameIntervalP = 1` |
| Network congestion | Reduce bitrate or resolution |
| VSync enabled | Disable VSync in renderer |

### Encoder Initialization Fails

**Symptoms**: "Failed to initialize NVENC"

**Solutions**:
1. Update NVIDIA drivers to latest
2. Check GPU supports NVENC (GTX 900+)
3. Check if another app is using NVENC
4. Try software encoder fallback

### Decoder Crashes

**Symptoms**: Client crashes during decoding

**Solutions**:
1. Ensure FFmpeg DLLs match header version
2. Check for corrupted packets (add CRC)
3. Add null checks before `avcodec_send_packet()`
4. Enable debug logs: `av_log_set_level(AV_LOG_DEBUG)`

---

## Verification Checklist

- [ ] NVENC encoder compiles without errors
- [ ] FFmpeg decoder links correctly
- [ ] DLLs copied to executable directory
- [ ] Host logs show "Using NVENC hardware encoder"
- [ ] Client logs show "FFmpeg decoder initialized successfully"
- [ ] Video displays correctly on client
- [ ] No memory leaks (check with Valgrind/Dr. Memory)
- [ ] Latency < 100ms on LAN
- [ ] Frame rate stable at 30-60 FPS
- [ ] CPU usage < 10% on both sides

---

## Next Steps

Once codecs are integrated:
1. Run performance benchmarks
2. Test on different network conditions
3. Tune encoder/decoder parameters
4. Add adaptive bitrate control
5. Implement error concealment
6. Add telemetry and metrics

## Support

For issues:
1. Check logs for error messages
2. Verify SDK/library versions
3. Test with reference applications
4. Contact NVIDIA Developer Forums (NVENC)
5. Check FFmpeg mailing list (decoder)
