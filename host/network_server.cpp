#include "network_server.h"
#include "../common/utils.h"
#include <algorithm>

// ============================================================================
// NetworkServer Implementation
// ============================================================================

NetworkServer::NetworkServer()
    : initialized(false)
    , running(false)
    , tcpSocket(INVALID_SOCKET)
    , udpSocket(INVALID_SOCKET)
    , lastStatsTime(0)
{
}

NetworkServer::~NetworkServer() {
    Shutdown();
}

bool NetworkServer::Initialize(const ServerConfig& cfg) {
    if (initialized) {
        LOG_WARNING("NetworkServer already initialized");
        return true;
    }

    LOG_INFO("Initializing network server...");
    config = cfg;

    // Initialize Winsock (should already be done, but check)
    if (!NetUtils::InitializeWinsock()) {
        LOG_ERROR("Failed to initialize Winsock");
        return false;
    }

    // Initialize TCP socket
    if (!InitializeTCP()) {
        LOG_ERROR("Failed to initialize TCP socket");
        return false;
    }

    // Initialize UDP socket
    if (!InitializeUDP()) {
        LOG_ERROR("Failed to initialize UDP socket");
        NetUtils::CloseSocket(tcpSocket);
        return false;
    }

    initialized = true;
    LOG_INFO_FMT("Network server initialized: TCP port %u, UDP port %u",
                 config.tcpPort, config.udpPort);

    return true;
}

void NetworkServer::Shutdown() {
    if (!initialized) {
        return;
    }

    LOG_INFO("Shutting down network server...");

    Stop();

    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        for (auto& client : clients) {
            if (client) {
                DisconnectClient(client.get());
            }
        }
        clients.clear();
    }

    // Close sockets
    NetUtils::CloseSocket(tcpSocket);
    NetUtils::CloseSocket(udpSocket);

    initialized = false;
    LOG_INFO("Network server shutdown complete");
}

bool NetworkServer::InitializeTCP() {
    // Create TCP socket
    tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!NetUtils::IsValidSocket(tcpSocket)) {
        LOG_ERROR_FMT("Failed to create TCP socket: %d", NetUtils::GetLastSocketError());
        return false;
    }

    // Set socket options
    NetUtils::SetSocketReuseAddr(tcpSocket, true);
    NetUtils::SetTcpNoDelay(tcpSocket, true);
    NetUtils::SetSocketTimeout(tcpSocket, 5000, 5000);

    // Bind to port
    if (!NetUtils::BindSocket(tcpSocket, config.tcpPort)) {
        LOG_ERROR_FMT("Failed to bind TCP socket to port %u: %d",
                     config.tcpPort, NetUtils::GetLastSocketError());
        NetUtils::CloseSocket(tcpSocket);
        return false;
    }

    // Listen for connections
    if (listen(tcpSocket, 5) == SOCKET_ERROR) {
        LOG_ERROR_FMT("Failed to listen on TCP socket: %d", NetUtils::GetLastSocketError());
        NetUtils::CloseSocket(tcpSocket);
        return false;
    }

    LOG_INFO_FMT("TCP socket listening on port %u", config.tcpPort);
    return true;
}

bool NetworkServer::InitializeUDP() {
    // Create UDP socket
    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!NetUtils::IsValidSocket(udpSocket)) {
        LOG_ERROR_FMT("Failed to create UDP socket: %d", NetUtils::GetLastSocketError());
        return false;
    }

    // Set socket options
    NetUtils::SetSocketReuseAddr(udpSocket, true);
    NetUtils::SetSocketBufferSize(udpSocket, 1024 * 1024, 1024 * 1024); // 1MB buffers

    // Bind to port
    if (!NetUtils::BindSocket(udpSocket, config.udpPort)) {
        LOG_ERROR_FMT("Failed to bind UDP socket to port %u: %d",
                     config.udpPort, NetUtils::GetLastSocketError());
        NetUtils::CloseSocket(udpSocket);
        return false;
    }

    LOG_INFO_FMT("UDP socket bound to port %u", config.udpPort);
    return true;
}

bool NetworkServer::Start() {
    if (!initialized) {
        LOG_ERROR("Server not initialized");
        return false;
    }

    if (running) {
        LOG_WARNING("Server already running");
        return true;
    }

    LOG_INFO("Starting network server...");
    running = true;

    // Start accept thread
    acceptThread = std::thread(&NetworkServer::AcceptThread, this);

    // Start keepalive thread
    keepaliveThread = std::thread(&NetworkServer::KeepaliveThread, this);

    LOG_INFO("Network server started");
    return true;
}

void NetworkServer::Stop() {
    if (!running) {
        return;
    }

    LOG_INFO("Stopping network server...");
    running = false;

    // Wait for threads to finish
    if (acceptThread.joinable()) {
        acceptThread.join();
    }

    if (keepaliveThread.joinable()) {
        keepaliveThread.join();
    }

    for (auto& thread : receiveThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    receiveThreads.clear();

    LOG_INFO("Network server stopped");
}

void NetworkServer::AcceptThread() {
    LOG_INFO("Accept thread started");

    while (running) {
        sockaddr_in clientAddr;
        int addrLen = sizeof(clientAddr);

        // Accept connection (with timeout via socket option)
        socket_t clientSocket = accept(tcpSocket, (sockaddr*)&clientAddr, &addrLen);

        if (!NetUtils::IsValidSocket(clientSocket)) {
            int error = NetUtils::GetLastSocketError();
            if (error == WSAETIMEDOUT || error == WSAEINTR) {
                continue; // Timeout, retry
            }
            if (running) {
                LOG_WARNING_FMT("Accept failed: %d", error);
            }
            break;
        }

        // Check if we can accept more clients
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            if (clients.size() >= config.maxClients) {
                LOG_WARNING("Max clients reached, rejecting connection");
                NetUtils::CloseSocket(clientSocket);
                continue;
            }
        }

        // Create client info
        auto client = std::make_unique<ClientInfo>();
        client->tcpSocket = clientSocket;
        client->tcpAddr = clientAddr;
        client->udpConnected = false;
        client->lastHeartbeat = PerformanceTimer::GetTimestampMicroseconds();
        client->authenticated = false;
        client->active = true;

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        LOG_INFO_FMT("Client connected from %s:%u", ipStr, ntohs(clientAddr.sin_port));

        // Handle handshake
        if (!HandleHandshake(client.get())) {
            LOG_WARNING("Handshake failed");
            NetUtils::CloseSocket(clientSocket);
            continue;
        }

        // Start receive thread for this client
        ClientInfo* clientPtr = client.get();
        receiveThreads.emplace_back(&NetworkServer::ReceiveThread, this, clientPtr);

        // Add to client list
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            clients.push_back(std::move(client));
        }

        LOG_INFO("Client accepted and authenticated");
    }

    LOG_INFO("Accept thread stopped");
}

void NetworkServer::ReceiveThread(ClientInfo* client) {
    LOG_INFO("Receive thread started for client");

    while (running && client->active) {
        // Receive control packet
        ControlPacket pkt;
        int bytesReceived = recv(client->tcpSocket, (char*)&pkt, sizeof(pkt), 0);

        if (bytesReceived <= 0) {
            int error = NetUtils::GetLastSocketError();
            if (error == WSAETIMEDOUT) {
                continue; // Timeout, retry
            }

            LOG_INFO_FMT("Client disconnected: %d", error);
            client->active = false;
            break;
        }

        if (bytesReceived != sizeof(ControlPacket)) {
            LOG_WARNING_FMT("Received invalid packet size: %d", bytesReceived);
            continue;
        }

        if (!pkt.IsValid()) {
            LOG_WARNING("Received invalid control packet");
            continue;
        }

        // Update heartbeat
        client->lastHeartbeat = PerformanceTimer::GetTimestampMicroseconds();

        // Handle packet
        HandleControlPacket(client, pkt);
    }

    LOG_INFO("Receive thread stopped for client");
}

void NetworkServer::KeepaliveThread() {
    LOG_INFO("Keepalive thread started");

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config.keepaliveIntervalMs));

        uint64_t now = PerformanceTimer::GetTimestampMicroseconds();

        std::lock_guard<std::mutex> lock(clientsMutex);
        for (auto& client : clients) {
            if (!client || !client->active) continue;

            // Check for timeout
            uint64_t timeSinceHeartbeat = now - client->lastHeartbeat;
            if (timeSinceHeartbeat > config.connectionTimeoutMs * 1000ULL) {
                LOG_WARNING("Client timeout, disconnecting");
                DisconnectClient(client.get());
                continue;
            }

            // Send keepalive
            SendKeepalive(client.get());
        }

        // Remove inactive clients
        clients.erase(
            std::remove_if(clients.begin(), clients.end(),
                          [](const std::unique_ptr<ClientInfo>& c) { return !c || !c->active; }),
            clients.end()
        );
    }

    LOG_INFO("Keepalive thread stopped");
}

bool NetworkServer::HandleHandshake(ClientInfo* client) {
    // Receive handshake packet
    HandshakePacket handshake;
    int bytesReceived = recv(client->tcpSocket, (char*)&handshake, sizeof(handshake), 0);

    if (bytesReceived != sizeof(HandshakePacket)) {
        LOG_ERROR_FMT("Invalid handshake size: %d", bytesReceived);
        return false;
    }

    if (!handshake.IsValid()) {
        LOG_ERROR("Invalid handshake packet");
        return false;
    }

    LOG_INFO_FMT("Handshake received: version %u.%u.%u",
                 handshake.clientVersion[0],
                 handshake.clientVersion[1],
                 handshake.clientVersion[2]);

    // Check version compatibility
    Version clientVersion(handshake.clientVersion[0],
                         handshake.clientVersion[1],
                         handshake.clientVersion[2]);

    if (!(clientVersion >= GUPT_VERSION)) {
        LOG_WARNING("Client version incompatible");
        SendHandshakeResponse(client, false);
        return false;
    }

    // Authentication
    if (config.requireAuth) {
        AuthPacket authPkt;
        bytesReceived = recv(client->tcpSocket, (char*)&authPkt, sizeof(authPkt), 0);

        if (bytesReceived != sizeof(AuthPacket)) {
            LOG_ERROR("Failed to receive auth packet");
            SendHandshakeResponse(client, false);
            return false;
        }

        if (!HandleAuth(client, authPkt)) {
            LOG_WARNING("Authentication failed");
            SendHandshakeResponse(client, false);
            return false;
        }
    } else {
        client->authenticated = true;
    }

    // Send success response
    return SendHandshakeResponse(client, true);
}

bool NetworkServer::HandleAuth(ClientInfo* client, const AuthPacket& authPkt) {
    if (!authPkt.IsValid()) {
        return false;
    }

    // Hash our password
    uint8_t expectedHash[32];
    CryptoUtils::SHA256String(config.password, expectedHash);

    // Compare hashes
    bool match = memcmp(expectedHash, authPkt.passwordHash, 32) == 0;

    if (match) {
        client->authenticated = true;
        LOG_INFO("Client authenticated successfully");
    } else {
        LOG_WARNING("Client authentication failed");
    }

    return match;
}

void NetworkServer::HandleControlPacket(ClientInfo* client, const ControlPacket& pkt) {
    switch (pkt.type) {
        case PACKET_TYPE_KEEPALIVE:
            // Update heartbeat (already done in receive thread)
            LOG_DEBUG("Keepalive received");
            break;

        case PACKET_TYPE_BITRATE:
            // Handle bitrate change request
            LOG_INFO_FMT("Bitrate change requested: %u kbps", pkt.payload / 1000);
            // TODO: Notify encoder to change bitrate
            break;

        case PACKET_TYPE_RESOLUTION:
            // Handle resolution change request
            LOG_INFO("Resolution change requested");
            // TODO: Notify capture/encoder to change resolution
            break;

        default:
            LOG_WARNING_FMT("Unknown control packet type: 0x%04X", pkt.type);
            break;
    }
}

bool NetworkServer::SendHandshakeResponse(ClientInfo* client, bool success) {
    ControlPacket response;
    response.type = PACKET_TYPE_HANDSHAKE;
    response.payload = success ? 1 : 0;
    response.timestamp = PerformanceTimer::GetTimestampMicroseconds();

    int bytesSent = send(client->tcpSocket, (char*)&response, sizeof(response), 0);
    if (bytesSent != sizeof(response)) {
        LOG_ERROR_FMT("Failed to send handshake response: %d", NetUtils::GetLastSocketError());
        return false;
    }

    return true;
}

void NetworkServer::SendKeepalive(ClientInfo* client) {
    ControlPacket pkt;
    pkt.type = PACKET_TYPE_KEEPALIVE;
    pkt.timestamp = PerformanceTimer::GetTimestampMicroseconds();

    int bytesSent = send(client->tcpSocket, (char*)&pkt, sizeof(pkt), 0);
    if (bytesSent != sizeof(pkt)) {
        LOG_DEBUG_FMT("Failed to send keepalive: %d", NetUtils::GetLastSocketError());
    }
}

void NetworkServer::DisconnectClient(ClientInfo* client) {
    if (!client) return;

    client->active = false;
    NetUtils::CloseSocket(client->tcpSocket);
    LOG_INFO("Client disconnected");
}

bool NetworkServer::SendFrame(const EncodedFrame& frame) {
    if (!running) {
        return false;
    }

    std::lock_guard<std::mutex> lock(clientsMutex);

    if (clients.empty()) {
        return false; // No clients connected
    }

    // Packetize and send to all clients
    return PacketizeAndSend(frame);
}

bool NetworkServer::PacketizeAndSend(const EncodedFrame& frame) {
    // Calculate number of packets needed
    uint32_t totalPackets = PacketUtils::CalculatePacketCount(frame.bitstreamSize);

    for (uint32_t i = 0; i < totalPackets; i++) {
        size_t offset = i * MAX_UDP_PAYLOAD;
        size_t dataSize = std::min((size_t)MAX_UDP_PAYLOAD, frame.data.size() - offset);

        // Create packet
        VideoPacket packet = PacketUtils::CreateVideoPacket(
            frame.frameNumber,
            i,
            totalPackets,
            frame.data.data() + offset,
            dataSize,
            frame.timestamp,
            frame.isKeyframe,
            i == totalPackets - 1
        );

        // Send to all clients
        if (!SendVideoPacket(packet)) {
            LOG_WARNING("Failed to send video packet");
            return false;
        }

        stats.packetsSent++;
    }

    stats.framesSent++;
    stats.bytesSent += frame.bitstreamSize;

    return true;
}

bool NetworkServer::SendVideoPacket(const VideoPacket& packet) {
    bool success = false;

    for (auto& client : clients) {
        if (!client || !client->active || !client->authenticated) {
            continue;
        }

        // Send via UDP
        size_t packetSize = sizeof(VideoPacketHeader) + packet.header.dataSize;
        int bytesSent = sendto(udpSocket,
                              (const char*)&packet,
                              (int)packetSize,
                              0,
                              (const sockaddr*)&client->tcpAddr, // Use TCP addr for now
                              sizeof(client->tcpAddr));

        if (bytesSent == packetSize) {
            success = true;
        } else {
            LOG_DEBUG_FMT("Failed to send UDP packet: %d", NetUtils::GetLastSocketError());
        }
    }

    return success;
}

bool NetworkServer::HasConnectedClients() const {
    std::lock_guard<std::mutex> lock(clientsMutex);
    return !clients.empty();
}

size_t NetworkServer::GetClientCount() const {
    std::lock_guard<std::mutex> lock(clientsMutex);
    return clients.size();
}

void StreamStats::Print() const {
    std::cout << "\n=== Stream Statistics ===\n";
    std::cout << "Frames sent:   " << framesSent.load() << "\n";
    std::cout << "Packets sent:  " << packetsSent.load() << "\n";
    std::cout << "Bytes sent:    " << bytesSent.load() << " (" << bytesSent.load() / 1024 / 1024 << " MB)\n";
    std::cout << "Packets lost:  " << packetsLost.load() << "\n";
    std::cout << "Current FPS:   " << currentFps.load() << "\n";
    std::cout << "Current bitrate: " << currentBitrate.load() / 1000000 << " Mbps\n";
    std::cout << "========================\n\n";
}

// ============================================================================
// Network Utilities
// ============================================================================

namespace NetworkUtils {

bool SendUdpHello(socket_t udpSocket, const sockaddr_in& addr) {
    UdpHelloPacket hello;
    hello.magic = GUPT_MAGIC;
    hello.clientId = 0;
    hello.timestamp = PerformanceTimer::GetTimestampMicroseconds();

    int bytesSent = sendto(udpSocket, (const char*)&hello, sizeof(hello), 0,
                          (const sockaddr*)&addr, sizeof(addr));

    return bytesSent == sizeof(hello);
}

bool ValidateUdpHello(const uint8_t* data, size_t size) {
    if (size < sizeof(UdpHelloPacket)) {
        return false;
    }

    const UdpHelloPacket* hello = reinterpret_cast<const UdpHelloPacket*>(data);
    return hello->magic == GUPT_MAGIC;
}

} // namespace NetworkUtils
