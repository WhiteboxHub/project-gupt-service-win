#pragma once

#include "../common/protocol.h"
#include "../common/packet.h"
#include "../common/logger.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <queue>
#include <memory>

// ============================================================================
// Network Client for Video Reception
// ============================================================================

struct ClientConfig {
    std::string serverHost = "127.0.0.1";
    uint16_t tcpPort = DEFAULT_TCP_PORT;
    uint16_t udpPort = DEFAULT_UDP_PORT;
    std::string password;
    uint32_t connectionTimeoutMs = 10000;
    uint32_t keepaliveIntervalMs = 2000;
    uint32_t jitterBufferMs = 50;  // Jitter buffer size
};

struct ReceivedFrame {
    std::vector<uint8_t> data;
    uint64_t frameNumber;
    uint64_t timestamp;
    bool isKeyframe;
    uint64_t receiveTime;
};

struct ClientStats {
    std::atomic<uint64_t> framesReceived{0};
    std::atomic<uint64_t> packetsReceived{0};
    std::atomic<uint64_t> bytesReceived{0};
    std::atomic<uint64_t> packetsLost{0};
    std::atomic<uint32_t> currentFps{0};
    std::atomic<uint32_t> currentBitrate{0};
    std::atomic<uint64_t> currentLatencyUs{0};

    void Reset() {
        framesReceived = 0;
        packetsReceived = 0;
        bytesReceived = 0;
        packetsLost = 0;
        currentFps = 0;
        currentBitrate = 0;
        currentLatencyUs = 0;
    }

    void Print() const;
};

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    // Connect to server
    bool Connect(const ClientConfig& config);

    // Disconnect from server
    void Disconnect();

    // Start receiving (spawns threads)
    bool Start();

    // Stop receiving
    void Stop();

    // Get next decoded frame (blocking)
    bool GetNextFrame(ReceivedFrame& outFrame, uint32_t timeoutMs = 100);

    // Send input event to server
    bool SendInputEvent(const InputPacket& packet);

    // Check if connected
    bool IsConnected() const { return connected; }

    // Get statistics
    const ClientStats& GetStats() const { return stats; }

private:
    // Connect TCP socket
    bool ConnectTCP();

    // Create UDP socket
    bool CreateUDP();

    // Perform handshake
    bool Handshake();

    // Authenticate
    bool Authenticate();

    // TCP receive thread
    void TcpReceiveThread();

    // UDP receive thread
    void UdpReceiveThread();

    // Keepalive thread
    void KeepaliveThread();

    // Send keepalive
    void SendKeepalive();

    // Handle control packet
    void HandleControlPacket(const ControlPacket& packet);

    // Frame assembler
    struct FrameAssembler {
        uint64_t frameId = 0;
        uint32_t totalPackets = 0;
        uint32_t receivedPackets = 0;
        bool isKeyframe = false;
        uint64_t firstPacketTime = 0;
        std::map<uint32_t, std::vector<uint8_t>> packets;

        bool AddPacket(const VideoPacket& packet);
        ReceivedFrame GetFrame() const;
        bool IsComplete() const { return receivedPackets == totalPackets; }
        bool IsExpired(uint64_t now, uint64_t timeoutUs) const;
    };

    // Jitter buffer
    struct JitterBuffer {
        std::queue<ReceivedFrame> frames;
        std::mutex mutex;
        uint32_t bufferTimeMs;

        JitterBuffer(uint32_t bufferMs) : bufferTimeMs(bufferMs) {}

        void Push(const ReceivedFrame& frame) {
            std::lock_guard<std::mutex> lock(mutex);
            frames.push(frame);
        }

        bool Pop(ReceivedFrame& frame, uint32_t timeoutMs) {
            std::lock_guard<std::mutex> lock(mutex);
            if (frames.empty()) {
                return false;
            }
            frame = frames.front();
            frames.pop();
            return true;
        }

        size_t Size() const {
            std::lock_guard<std::mutex> lock(mutex);
            return frames.size();
        }
    };

private:
    ClientConfig config;
    std::atomic<bool> connected;
    std::atomic<bool> running;

    // Sockets
    socket_t tcpSocket;
    socket_t udpSocket;
    sockaddr_in serverAddr;

    // Threads
    std::thread tcpReceiveThread;
    std::thread udpReceiveThread;
    std::thread keepaliveThread;

    // Frame assembly
    std::map<uint64_t, FrameAssembler> frameAssemblers;
    std::mutex assemblerMutex;

    // Jitter buffer
    std::unique_ptr<JitterBuffer> jitterBuffer;

    // Statistics
    ClientStats stats;
    uint64_t lastHeartbeat;
};
