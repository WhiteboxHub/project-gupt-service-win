#pragma once

#include "../common/protocol.h"
#include "../common/packet.h"
#include "../common/logger.h"
#include "encoder.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

// ============================================================================
// Network Server for Video Streaming
// ============================================================================

struct ServerConfig {
    uint16_t tcpPort = DEFAULT_TCP_PORT;        // 5900
    uint16_t udpPort = DEFAULT_UDP_PORT;        // 5901
    uint32_t maxClients = 1;                    // Single client for now
    bool requireAuth = false;                   // Password authentication
    std::string password;                       // Authentication password
    uint32_t keepaliveIntervalMs = 2000;        // Keepalive interval
    uint32_t connectionTimeoutMs = 10000;       // Connection timeout
};

struct ClientInfo {
    socket_t tcpSocket;
    sockaddr_in tcpAddr;
    sockaddr_in udpAddr;
    bool udpConnected;
    std::string clientId;
    uint64_t lastHeartbeat;
    bool authenticated;
    std::atomic<bool> active;
};

struct StreamStats {
    std::atomic<uint64_t> framesSent{0};
    std::atomic<uint64_t> packetsSent{0};
    std::atomic<uint64_t> bytesSent{0};
    std::atomic<uint64_t> packetsLost{0};
    std::atomic<uint32_t> currentBitrate{0};
    std::atomic<uint32_t> currentFps{0};

    void Reset() {
        framesSent = 0;
        packetsSent = 0;
        bytesSent = 0;
        packetsLost = 0;
        currentBitrate = 0;
        currentFps = 0;
    }

    void Print() const;
};

class NetworkServer {
public:
    NetworkServer();
    ~NetworkServer();

    // Delete copy/move
    NetworkServer(const NetworkServer&) = delete;
    NetworkServer& operator=(const NetworkServer&) = delete;

    // Initialize server
    bool Initialize(const ServerConfig& config);

    // Shutdown server
    void Shutdown();

    // Start accepting connections
    bool Start();

    // Stop server
    void Stop();

    // Send encoded frame to all connected clients
    bool SendFrame(const EncodedFrame& frame);

    // Check if any clients are connected
    bool HasConnectedClients() const;

    // Get number of connected clients
    size_t GetClientCount() const;

    // Get streaming statistics
    const StreamStats& GetStats() const { return stats; }

    // Check if initialized
    bool IsInitialized() const { return initialized; }

    // Check if running
    bool IsRunning() const { return running; }

private:
    // Initialize TCP socket
    bool InitializeTCP();

    // Initialize UDP socket
    bool InitializeUDP();

    // Accept thread function
    void AcceptThread();

    // Receive thread function (TCP control messages)
    void ReceiveThread(ClientInfo* client);

    // Keepalive thread function
    void KeepaliveThread();

    // Handle handshake
    bool HandleHandshake(ClientInfo* client);

    // Handle authentication
    bool HandleAuth(ClientInfo* client, const AuthPacket& authPkt);

    // Handle control packet
    void HandleControlPacket(ClientInfo* client, const ControlPacket& pkt);

    // Send handshake response
    bool SendHandshakeResponse(ClientInfo* client, bool success);

    // Send keepalive
    void SendKeepalive(ClientInfo* client);

    // Disconnect client
    void DisconnectClient(ClientInfo* client);

    // Packetize and send frame
    bool PacketizeAndSend(const EncodedFrame& frame);

    // Send single video packet via UDP
    bool SendVideoPacket(const VideoPacket& packet);

private:
    ServerConfig config;
    bool initialized;
    std::atomic<bool> running;

    // Sockets
    socket_t tcpSocket;
    socket_t udpSocket;

    // Clients
    std::vector<std::unique_ptr<ClientInfo>> clients;
    mutable std::mutex clientsMutex;

    // Threads
    std::thread acceptThread;
    std::thread keepaliveThread;
    std::vector<std::thread> receiveThreads;

    // Statistics
    StreamStats stats;
    std::atomic<uint64_t> lastStatsTime;
};

// ============================================================================
// Network Utilities
// ============================================================================

namespace NetworkUtils {

    // Create UDP "hello" packet for NAT traversal
    struct UdpHelloPacket {
        uint32_t magic;
        uint32_t clientId;
        uint64_t timestamp;
    };

    // Send UDP hello packet
    bool SendUdpHello(socket_t udpSocket, const sockaddr_in& addr);

    // Validate UDP hello packet
    bool ValidateUdpHello(const uint8_t* data, size_t size);

} // namespace NetworkUtils
