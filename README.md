# GuPT - High-Performance Windows Remote Desktop

**GuPT** (pronounced "gupt", meaning "secret" in Hindi) is a high-performance, low-latency Windows-to-Windows remote desktop application built with modern C++, DirectX, NVIDIA NVENC, and optimized networking.

## Features

- **Hardware-Accelerated Capture**: DXGI Desktop Duplication API for zero-copy screen capture
- **GPU Encoding**: NVIDIA NVENC for H.264 hardware encoding with ultra-low latency
- **Optimized Streaming**: UDP for video, TCP for control with custom packetization
- **Low Latency**: Target <100ms glass-to-glass latency
- **High Frame Rate**: 30-60 FPS at up to 4K resolution
- **Full Input Control**: Mouse, keyboard, and scroll with accurate injection
- **P2P Connection**: Direct connection or via signaling server for NAT traversal
- **Performance Monitoring**: Real-time metrics for latency, FPS, and bandwidth

## Architecture

```
┌─────────────────┐                            ┌─────────────────┐
│   HOST (Server) │                            │ CLIENT (Viewer) │
├─────────────────┤                            ├─────────────────┤
│ Screen Capture  │                            │  Video Decoder  │
│  (DXGI)         │                            │  (NVDEC/FFmpeg) │
│       ↓         │                            │       ↓         │
│  NVENC Encoder  │                            │  D3D11 Renderer │
│       ↓         │    UDP Video Stream       │       ↓         │
│  UDP Sender     ├───────────────────────────►│  UDP Receiver   │
│                 │                            │                 │
│  Input Handler  │    TCP Control Channel     │  Input Capture  │
│       ↑         │◄───────────────────────────┤       ↑         │
└─────────────────┘                            └─────────────────┘
```

## Tech Stack

- **Language**: C++17
- **Graphics**: Direct3D 11, DXGI
- **Encoding**: NVIDIA NVENC, FFmpeg (fallback)
- **Decoding**: NVIDIA NVDEC, FFmpeg
- **Networking**: Winsock2 (TCP + UDP)
- **Build System**: CMake, Visual Studio 2019+

## Project Structure

```
GuPT/
├── common/              # Shared code between host and client
│   ├── protocol.h       # Protocol constants and definitions
│   ├── packet.h         # Network packet structures
│   ├── utils.h/.cpp     # Utility functions (networking, timing, crypto)
│   └── logger.h/.cpp    # Thread-safe logging system
│
├── host/                # Host (server) application
│   ├── main.cpp         # Host entry point
│   ├── capture.*        # DXGI screen capture
│   ├── encoder.*        # NVENC H.264 encoding
│   ├── network_server.* # TCP/UDP server
│   └── input_handler.*  # Input injection (SendInput)
│
├── client/              # Client (viewer) application
│   ├── main.cpp         # Client entry point
│   ├── decoder.*        # H.264 decoding (NVDEC/FFmpeg)
│   ├── renderer.*       # D3D11 rendering
│   ├── network_client.* # TCP/UDP client
│   └── input_sender.*   # Input capture and transmission
│
├── signaling/           # Signaling server (Go)
│   └── main.go          # HTTP/WebSocket signaling for P2P
│
└── third_party/         # External dependencies
    ├── nvenc/           # NVIDIA Video Codec SDK
    ├── cuda/            # CUDA headers
    └── ffmpeg/          # FFmpeg (optional)
```

## Development Status

### ✅ Phase 1: Foundation (Completed)
- [x] Project structure
- [x] Common headers (protocol, packet)
- [x] Utility functions (networking, timing, crypto)
- [x] Logging system
- [x] CMake build system

### 📋 Next: Phase 2 - Screen Capture
- [ ] DXGI Desktop Duplication
- [ ] Frame acquisition loop
- [ ] Dirty region tracking

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) - Complete system architecture (60+ pages)
- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) - Step-by-step implementation guide
- See docs above for detailed technical specifications

## Quick Start

**Prerequisites**: Windows 10+, NVIDIA GPU, Visual Studio 2019+, NVIDIA Video Codec SDK

```bash
# 1. Clone and setup
git clone <repo>
cd project-gupt-service-win

# 2. Install NVIDIA SDK to third_party/nvenc/

# 3. Build
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64
cmake --build . --config Release
```

## License

[MIT License](LICENSE)
project-gupt-service-win
