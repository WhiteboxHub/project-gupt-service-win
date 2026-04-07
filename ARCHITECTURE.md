# GUPT Remote Desktop - System Architecture

## Executive Summary

**GUPT** is a high-performance, low-latency Windows-to-Windows remote desktop solution leveraging hardware acceleration and modern networking techniques.

**Key Performance Targets:**
- Latency: <100ms end-to-end
- Frame Rate: 30-60 FPS
- Resolution: Up to 4K (3840x2160)
- Bandwidth: 2-10 Mbps (adaptive)

---

## 1. SYSTEM ARCHITECTURE OVERVIEW

```
┌─────────────────────────────────────────────────────────────────┐
│                      SIGNALING SERVER (Optional)                 │
│                         (Go/C++ based)                           │
│  - Session Registration                                          │
│  - Peer Discovery                                                │
│  - NAT Information Exchange                                      │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 │ HTTPS/WSS
                 │
    ┌────────────┴────────────┐
    │                         │
    ▼                         ▼
┌─────────────┐           ┌─────────────┐
│    HOST     │◄─────────►│   CLIENT    │
│  (Server)   │  P2P/LAN  │ (Receiver)  │
└─────────────┘           └─────────────┘
     │                         │
     │ TCP (Control)           │
     │ UDP (Video Stream)      │
     └─────────────────────────┘
```

---

## 2. CONNECTION MODES

### Mode 1: Direct Connection
```
CLIENT → (IP:PORT) → HOST
```
- Used for LAN or when public IP is known
- No intermediary server required
- Lowest latency

### Mode 2: Signaling Server Mode
```
HOST → Register Session → SIGNALING SERVER
CLIENT → Request Session → SIGNALING SERVER
SIGNALING SERVER → Returns Peer Info
CLIENT ←─────── P2P Connection ─────→ HOST
```
- Used for NAT traversal
- Enables connection across internet
- Falls back to relay if P2P fails

### Connection Strategy
1. Try signaling server (if configured)
2. Fallback to direct IP connection
3. Support manual IP:PORT override

---

## 3. HOST (SERVER) ARCHITECTURE

### 3.1 Component Overview

```
┌──────────────────────────────────────────────────────────┐
│                    HOST APPLICATION                       │
├──────────────────────────────────────────────────────────┤
│                                                           │
│  ┌─────────────┐      ┌──────────────┐                  │
│  │   Capture   │─────►│   Encoder    │                  │
│  │   Thread    │      │   Thread     │                  │
│  │  (DXGI)     │      │  (NVENC)     │                  │
│  └─────────────┘      └──────┬───────┘                  │
│        │                     │                           │
│        │                     ▼                           │
│        │              ┌──────────────┐                   │
│        │              │   Network    │                   │
│        │              │   Sender     │──► UDP Packets   │
│        │              │   Thread     │                   │
│        │              └──────────────┘                   │
│        │                                                 │
│        ▼                     ▲                           │
│  ┌─────────────┐      ┌──────────────┐                  │
│  │  Dirty      │      │   Input      │◄── TCP Control   │
│  │  Region     │      │   Handler    │                  │
│  │  Tracker    │      │   Thread     │                  │
│  └─────────────┘      └──────────────┘                  │
│                                                           │
└──────────────────────────────────────────────────────────┘
```

### 3.2 Capture Module (capture.cpp)

**Technology:** DXGI Desktop Duplication API

**Key Classes:**
- `ScreenCapture`
  - `Initialize()`: Setup DXGI device and output duplication
  - `CaptureFrame()`: Acquire next frame from GPU
  - `GetDirtyRects()`: Get changed regions
  - `ReleaseFrame()`: Release frame back to system

**Threading Model:**
- Dedicated capture thread running at 60Hz polling
- Uses `IDXGIOutputDuplication::AcquireNextFrame()`
- Handles resolution changes dynamically
- Supports multi-monitor enumeration

**Performance Optimizations:**
- Only capture dirty regions when available
- Zero-copy GPU texture access
- Fallback to full frame on timeout

**Error Handling:**
- Access lost: Recreate duplication interface
- Display mode change: Reinitialize with new dimensions
- GPU reset: Rebuild entire D3D11 pipeline

### 3.3 Encoder Module (encoder.cpp)

**Technology:** NVIDIA NVENC (NvEncodeAPI.h)

**Key Classes:**
- `NvencEncoder`
  - `Initialize()`: Setup NVENC session with D3D11 interop
  - `EncodeFrame()`: Encode D3D11 texture to H.264
  - `GetEncodedPacket()`: Retrieve compressed bitstream
  - `Reconfigure()`: Adjust bitrate/resolution dynamically

**Configuration:**
- **Codec:** H.264 (HEVC optional for future)
- **Preset:** P1 (ultra-low latency)
- **Rate Control:** CBR with low VBV buffer
- **GOP Structure:** Infinite GOP with periodic IDR (every 2s)
- **Bitrate:** Adaptive 2-10 Mbps based on bandwidth

**NVENC Session Parameters:**
```cpp
NV_ENC_INITIALIZE_PARAMS:
  - encodeGUID = NV_ENC_CODEC_H264_GUID
  - presetGUID = NV_ENC_PRESET_P1_GUID (low latency)
  - tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY
  - maxEncodeWidth = 3840
  - maxEncodeHeight = 2160

NV_ENC_CONFIG:
  - rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR
  - rcParams.averageBitRate = 5000000 (5 Mbps default)
  - rcParams.vbvBufferSize = 2 * averageBitRate / frameRate
  - gopLength = NVENC_INFINITE_GOPLENGTH
  - frameIntervalP = 1 (no B-frames)
  - enableIntraRefresh = 1
```

**Fallback Path:**
- Software encoding via FFmpeg libx264 if NVENC unavailable
- Same latency-optimized parameters (tune=zerolatency)

### 3.4 Network Server Module (network_server.cpp)

**Key Classes:**
- `NetworkServer`
  - TCP control socket
  - UDP video socket
  - Client session management
  - Connection state machine

**Protocol Stack:**

**TCP Channel (Port: BASE_PORT, e.g., 5900):**
- Connection handshake
- Authentication (password)
- Input events (mouse, keyboard)
- Control messages (resolution change, reconnect)
- Keep-alive heartbeat

**UDP Channel (Port: BASE_PORT + 1, e.g., 5901):**
- Video frame packets
- Sequence numbers for ordering
- No retransmission (real-time)

**Packet Format (UDP):**
```cpp
struct VideoPacket {
    uint32_t magic;           // 0x47555054 ("GUPT")
    uint16_t version;         // Protocol version
    uint16_t type;            // FRAME_DATA, KEYFRAME, etc.
    uint64_t frameId;         // Monotonic frame counter
    uint32_t sequenceNum;     // Packet sequence within frame
    uint32_t totalPackets;    // Total packets for this frame
    uint32_t dataSize;        // Payload size
    uint64_t timestamp;       // Capture timestamp (us)
    uint8_t data[MAX_PACKET_SIZE]; // H.264 NAL units
};
```

**Threading Model:**
- Accept thread: Handle new connections
- Send thread: Dequeue encoded frames, packetize, send UDP
- Receive thread: Handle TCP control messages
- Input dispatch thread: Inject keyboard/mouse events

### 3.5 Input Handler Module (input_handler.cpp)

**Key Classes:**
- `InputHandler`
  - `InjectMouseMove(x, y)`
  - `InjectMouseButton(button, down)`
  - `InjectMouseWheel(delta)`
  - `InjectKeyboard(vkCode, down)`

**Windows API:**
- `SendInput()` with `INPUT` structures
- Absolute coordinates using `MOUSEEVENTF_ABSOLUTE`
- Handle multiple monitors with coordinate scaling
- Virtual key code mapping

**Security:**
- Optional UIPI bypass for elevated applications
- Input validation to prevent injection attacks

---

## 4. CLIENT (RECEIVER) ARCHITECTURE

### 4.1 Component Overview

```
┌──────────────────────────────────────────────────────────┐
│                   CLIENT APPLICATION                      │
├──────────────────────────────────────────────────────────┤
│                                                           │
│  ┌─────────────┐      ┌──────────────┐                  │
│  │   Network   │─────►│   Decoder    │                  │
│  │   Receiver  │      │   Thread     │                  │
│  │   Thread    │      │ (NVDEC/FFmpeg)│                 │
│  └──────┬──────┘      └──────┬───────┘                  │
│         │                    │                           │
│         │ UDP Packets        │ Decoded Frames            │
│         │                    ▼                           │
│         │             ┌──────────────┐                   │
│         │             │   Renderer   │                   │
│         │             │   Thread     │──► Display        │
│         │             │  (D3D11)     │                   │
│         │             └──────────────┘                   │
│         │                                                │
│         ▼                     │                          │
│  ┌─────────────┐      ┌──────────────┐                  │
│  │  Jitter     │      │   Input      │──► TCP Control   │
│  │  Buffer     │      │   Capture    │                  │
│  │             │      │              │                   │
│  └─────────────┘      └──────────────┘                  │
│                                                           │
└──────────────────────────────────────────────────────────┘
```

### 4.2 Network Client Module (network_client.cpp)

**Key Classes:**
- `NetworkClient`
  - `ConnectDirect(ip, port)`
  - `ConnectViaSignaling(sessionId)`
  - `ReceiveVideoPackets()`
  - `SendInputEvent()`

**Connection Flow:**
1. Resolve connection method (direct vs signaling)
2. Establish TCP control connection
3. Authenticate (send password hash)
4. Bind UDP socket for video stream
5. Send UDP "hello" packets for NAT traversal
6. Start receive loops

**Jitter Buffer:**
- Reorder packets by sequence number
- Buffer 2-3 frames worth of packets
- Drop stale packets if frame already displayed
- Adaptive buffer size based on jitter measurement

### 4.3 Decoder Module (decoder.cpp)

**Technology:** NVDEC (CUDA-accelerated) or FFmpeg

**Key Classes:**
- `VideoDecoder`
  - `Initialize(codec, width, height)`
  - `DecodePacket(data, size)`
  - `GetDecodedFrame()`: Returns D3D11 texture

**NVDEC Path:**
- Use CUDA Video Decoder API
- Direct decode to GPU surface
- Zero-copy via D3D11-CUDA interop

**FFmpeg Path:**
- Use `avcodec_decode_video2()`
- Upload decoded frame to GPU texture

### 4.4 Renderer Module (renderer.cpp)

**Technology:** Direct3D 11

**Key Classes:**
- `D3DRenderer`
  - `Initialize(hwnd)`: Create swap chain, device
  - `RenderFrame(texture)`: Draw texture to window
  - `Resize()`: Handle window resize

**Rendering Pipeline:**
1. Copy decoded texture to back buffer
2. Apply optional scaling/filtering
3. Present to window
4. Measure and display FPS/latency stats

**VSync Handling:**
- Disable VSync for lowest latency
- Optional triple buffering for smoother output

### 4.5 Input Sender Module (input_sender.cpp)

**Key Classes:**
- `InputCapture`
  - Hook raw input messages
  - Package mouse/keyboard events
  - Send via TCP

**Windows API:**
- `SetWindowsHookEx()` for keyboard
- `GetCursorPos()` and window messages for mouse
- Relative coordinate calculation

**Input Packet Format (TCP):**
```cpp
struct InputPacket {
    uint16_t type;        // MOUSE_MOVE, MOUSE_BUTTON, KEY, etc.
    uint16_t flags;       // Button state, modifiers
    int32_t x, y;         // Mouse coordinates (relative to screen)
    uint32_t vkCode;      // Virtual key code (keyboard)
    uint64_t timestamp;   // Client timestamp
};
```

---

## 5. COMMON MODULES

### 5.1 Protocol (protocol.h)

**Defines:**
- Packet types and magic numbers
- Protocol version
- Maximum packet sizes
- Default ports

**Constants:**
```cpp
#define GUPT_MAGIC 0x47555054
#define PROTOCOL_VERSION 1
#define DEFAULT_TCP_PORT 5900
#define DEFAULT_UDP_PORT 5901
#define MAX_UDP_PACKET_SIZE 1400  // MTU - headers
```

### 5.2 Packet Structures (packet.h)

All network packet definitions:
- `VideoPacket`
- `InputPacket`
- `ControlPacket`
- `HandshakePacket`

### 5.3 Utilities (utils/)

- `ThreadSafeQueue<T>`: Lock-free SPSC queue
- `PerformanceTimer`: High-resolution timing
- `Logger`: Multi-level logging to file/console
- `CryptoUtils`: Optional AES encryption

---

## 6. SIGNALING SERVER PROTOCOL

### 6.1 Message Types

**Host Register:**
```json
{
  "type": "register",
  "session_id": "unique-session-id",
  "password_hash": "sha256-hash",
  "local_port": 5900,
  "version": 1
}
```

**Server Response:**
```json
{
  "type": "registered",
  "session_id": "unique-session-id",
  "public_ip": "203.0.113.50",
  "expires_in": 3600
}
```

**Client Connect:**
```json
{
  "type": "connect",
  "session_id": "unique-session-id",
  "password_hash": "sha256-hash"
}
```

**Server Peer Info:**
```json
{
  "type": "peer_info",
  "host_ip": "203.0.113.50",
  "host_port": 5900,
  "session_valid": true
}
```

### 6.2 NAT Traversal

**UDP Hole Punching:**
1. Both peers send UDP packets to each other
2. This creates port mappings in NATs
3. Subsequent packets can traverse NAT

**STUN-like Behavior:**
- Server detects external IP:PORT
- Shares with peer
- Direct P2P connection established

**Relay Fallback:**
- For symmetric NATs where P2P fails
- Server proxies video stream (high bandwidth cost)
- Not implemented in v1

---

## 7. THREADING MODEL

### 7.1 Host Threads

| Thread | Purpose | Priority | Blocking |
|--------|---------|----------|----------|
| Capture | DXGI frame acquisition | High | Yes (16ms max) |
| Encoder | NVENC encoding | High | No (async) |
| Network Send | UDP packet transmission | Normal | No |
| Network Recv | TCP control messages | Normal | Yes (blocking IO) |
| Input Inject | SendInput dispatch | High | No |
| Main | UI, orchestration | Normal | Event loop |

### 7.2 Client Threads

| Thread | Purpose | Priority | Blocking |
|--------|---------|----------|----------|
| Network Recv | UDP video packets | High | Yes (blocking IO) |
| Decoder | H.264 decoding | High | No (async) |
| Renderer | D3D11 present | Normal | VSync dependent |
| Input Send | Send input to host | Normal | No |
| Main | UI, window messages | Normal | Event loop |

### 7.3 Synchronization

**Data Flow:**
- Lock-free queues between threads where possible
- Frame buffers: Triple buffering with atomic swap
- Minimize critical sections (use RAII guards)

**Queue Sizes:**
- Capture → Encoder: 3 frames
- Encoder → Network: 5 frames
- Network → Decoder: Adaptive jitter buffer
- Decoder → Renderer: 2 frames

---

## 8. PERFORMANCE OPTIMIZATIONS

### 8.1 Latency Reduction

1. **Zero-Copy Paths:**
   - DXGI texture → NVENC input (D3D11 interop)
   - NVDEC output → D3D11 renderer (CUDA interop)

2. **Minimal Buffering:**
   - No frame buffering in encoder
   - Jitter buffer only on client
   - Immediate SendInput injection

3. **Hardware Acceleration:**
   - GPU capture (DXGI)
   - GPU encoding (NVENC)
   - GPU decoding (NVDEC)
   - GPU rendering (D3D11)

### 8.2 Bandwidth Optimization

1. **Dirty Region Detection:**
   - Only encode changed screen areas
   - DXGI provides dirty rects
   - Reduces encoding load for static content

2. **Adaptive Bitrate:**
   - Monitor packet loss and RTT
   - Adjust NVENC bitrate dynamically
   - Scale resolution if bandwidth insufficient

3. **GOP Structure:**
   - Intra-refresh instead of periodic I-frames
   - Reduces bitrate spikes
   - Better quality consistency

### 8.3 Error Recovery

1. **Packet Loss:**
   - Client detects missing sequences
   - Request keyframe via TCP
   - Host sends IDR frame

2. **Connection Loss:**
   - TCP keepalive detects dead connection
   - Automatic reconnection with exponential backoff
   - Resume from last known state

3. **GPU Errors:**
   - Device lost: Rebuild D3D11 device
   - Encoder error: Fallback to software
   - Decoder error: Request keyframe

---

## 9. SECURITY CONSIDERATIONS

### 9.1 Authentication

- **Password-based:** SHA-256 hash sent over TCP
- **Session tokens:** Time-limited access
- **Brute-force protection:** Rate limiting on signaling server

### 9.2 Encryption (Future)

- **TLS for TCP:** Control channel encryption
- **DTLS for UDP:** Video stream encryption (high overhead)
- **AES-GCM:** Custom lightweight encryption

### 9.3 Input Validation

- Validate all input coordinates within screen bounds
- Sanitize virtual key codes
- Prevent injection of system keys (Win+L, etc.)

---

## 10. TESTING STRATEGY

### 10.1 Unit Tests

- Packet serialization/deserialization
- Queue operations
- Coordinate transformations

### 10.2 Integration Tests

1. **Loopback Test:**
   - Run host and client on same machine
   - Connect via 127.0.0.1
   - Verify low latency (<10ms)

2. **LAN Test:**
   - Two machines on same network
   - Measure latency and packet loss
   - Test input responsiveness

3. **WAN Test:**
   - Machines on different networks
   - Via signaling server
   - Test NAT traversal

### 10.3 Performance Profiling

- **ETW Tracing:** Capture Windows performance data
- **NVIDIA Nsight:** Profile GPU workloads
- **Custom Metrics:** Log frame timing, jitter, packet loss

---

## 11. BUILD SYSTEM

### 11.1 Dependencies

**Required:**
- Visual Studio 2019 or later (C++17)
- Windows SDK 10.0.19041 or later
- DirectX SDK (included in Windows SDK)
- NVIDIA Video Codec SDK 12.x
- CUDA Toolkit 11.8+ (for NVDEC)

**Optional:**
- FFmpeg 5.x (software encoding/decoding fallback)
- OpenSSL 3.x (encryption)

### 11.2 Project Structure
```
GuPT/
├── CMakeLists.txt
├── README.md
├── ARCHITECTURE.md (this file)
├── LICENSE
│
├── common/
│   ├── protocol.h
│   ├── packet.h
│   ├── utils.h
│   └── utils.cpp
│
├── host/
│   ├── main.cpp
│   ├── capture.h / capture.cpp
│   ├── encoder.h / encoder.cpp
│   ├── network_server.h / network_server.cpp
│   └── input_handler.h / input_handler.cpp
│
├── client/
│   ├── main.cpp
│   ├── decoder.h / decoder.cpp
│   ├── renderer.h / renderer.cpp
│   ├── network_client.h / network_client.cpp
│   └── input_sender.h / input_sender.cpp
│
├── signaling/
│   └── (Go-based signaling server - separate)
│
└── third_party/
    ├── nvenc/ (NVIDIA Video Codec SDK headers)
    ├── cuda/
    └── ffmpeg/ (optional)
```

---

## 12. IMPLEMENTATION ROADMAP

### Phase 1: Foundation (Week 1)
- [ ] Project setup (CMake, VS solution)
- [ ] Common packet structures
- [ ] Basic TCP client/server
- [ ] Basic UDP client/server
- [ ] Loopback test

### Phase 2: Screen Capture (Week 2)
- [ ] DXGI Desktop Duplication setup
- [ ] Frame acquisition loop
- [ ] Dirty region tracking
- [ ] Multi-monitor enumeration
- [ ] Error handling (mode changes, access lost)

### Phase 3: Encoding (Week 2-3)
- [ ] NVENC initialization
- [ ] D3D11 texture input binding
- [ ] H.264 encoding with low-latency preset
- [ ] Bitstream extraction
- [ ] Software fallback (FFmpeg x264)

### Phase 4: Streaming (Week 3)
- [ ] Frame packetization
- [ ] UDP transmission
- [ ] Sequence numbering
- [ ] Basic flow control

### Phase 5: Decoding & Rendering (Week 4)
- [ ] NVDEC/FFmpeg decoder setup
- [ ] Packet reassembly
- [ ] Jitter buffer
- [ ] D3D11 renderer
- [ ] Window management

### Phase 6: Input Control (Week 4-5)
- [ ] Client input capture
- [ ] Input packet serialization
- [ ] TCP input transmission
- [ ] Host input injection (SendInput)
- [ ] Coordinate mapping

### Phase 7: Signaling Server (Week 5)
- [ ] Go-based HTTP/WebSocket server
- [ ] Session registration
- [ ] Peer discovery
- [ ] NAT traversal helpers
- [ ] Client/host signaling integration

### Phase 8: Polish (Week 6)
- [ ] Adaptive bitrate
- [ ] Reconnection logic
- [ ] Performance tuning
- [ ] UI/UX improvements
- [ ] Documentation

---

## 13. PERFORMANCE BENCHMARKS

**Target Metrics:**

| Metric | Target | Excellent |
|--------|--------|-----------|
| Glass-to-glass latency | <100ms | <50ms |
| Frame rate | 30 FPS | 60 FPS |
| Capture overhead | <5ms | <2ms |
| Encoding overhead | <10ms | <5ms |
| Network jitter | <20ms | <10ms |
| Decoding overhead | <8ms | <3ms |
| Packet loss tolerance | <1% | <0.1% |
| CPU usage (host) | <30% | <15% |
| CPU usage (client) | <20% | <10% |

---

## 14. FUTURE ENHANCEMENTS

### v2.0
- Multi-monitor streaming (separate encoders)
- Audio streaming (WASAPI capture)
- File transfer
- Clipboard synchronization
- Hardware cursor rendering

### v3.0
- HEVC/AV1 encoding for better compression
- Peer-to-peer file sharing
- Mobile client (Android/iOS)
- Web client (WebRTC)
- Session recording

---

## 15. GLOSSARY

- **DXGI:** DirectX Graphics Infrastructure - Windows API for display management
- **NVENC:** NVIDIA Hardware Video Encoder
- **NVDEC:** NVIDIA Hardware Video Decoder
- **CBR:** Constant Bitrate
- **GOP:** Group of Pictures
- **IDR:** Instantaneous Decoder Refresh (keyframe)
- **NAL:** Network Abstraction Layer (H.264 packet)
- **STUN:** Session Traversal Utilities for NAT
- **VBV:** Video Buffering Verifier
- **VSync:** Vertical Synchronization

---

**Document Version:** 1.0
**Last Updated:** April 6, 2026
**Author:** Senior Systems Engineer
