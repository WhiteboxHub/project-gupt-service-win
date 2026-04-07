#include "network_client.h"
#include "../common/utils.h"
#include <algorithm>

// ============================================================================
// NetworkClient Implementation
// ============================================================================

NetworkClient::NetworkClient()
    : connected(false)
    , running(false)
    , tcpSocket(INVALID_SOCKET)
    , udpSocket(INVALID_SOCKET)
    , lastHeartbeat(0)
{
}

NetworkClient::~NetworkClient() {
    Disconnect();
}

bool NetworkClient::Connect(const ClientConfig& cfg) {
    if (connected) {
        LOG_WARNING("Already connected");
        return true;
    }

    LOG_INFO("Connecting to server...");
    config = cfg;

    // Initialize Winsock
    if (!NetUtils::InitializeWinsock()) {
        LOG_ERROR("Failed to initialize Winsock");
        return false;
    }

    // Connect TCP
    if (!ConnectTCP()) {
        LOG_ERROR("Failed to connect TCP");
        return false;
    }

    // Perform handshake
    if (!Handshake()) {
        LOG_ERROR("Handshake failed");
        NetUtils::CloseSocket(tcpSocket);
        return false;
    }

    // Authenticate (if password provided)
    if (!config.password.empty()) {
        if (!Authenticate()) {
            LOG_ERROR("Authentication failed");
            NetUtils::CloseSocket(tcpSocket);
            return false;
        }
    }

    // Create UDP socket
    if (!CreateUDP()) {
        LOG_ERROR("Failed to create UDP socket");
        NetUtils::CloseSocket(tcpSocket);
        return false;
    }

    // Initialize jitter buffer
    jitterBuffer = std::make_unique<JitterBuffer>(config.jitterBufferMs);

    connected = true;
    lastHeartbeat = PerformanceTimer::GetTimestampMicroseconds();

    LOG_INFO("Connected to server successfully");
    return true;
}

void NetworkClient::Disconnect() {
    if (!connected) {
        return;
    }

    LOG_INFO("Disconnecting from server...");

    Stop();

    connected = false;
    NetUtils::CloseSocket(tcpSocket);
    NetUtils::CloseSocket(udpSocket);

    LOG_INFO("Disconnected");
}

bool NetworkClient::ConnectTCP() {
    // Create TCP socket
    tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!NetUtils::IsValidSocket(tcpSocket)) {
        LOG_ERROR_FMT("Failed to create TCP socket: %d", NetUtils::GetLastSocketError());
        return false;
    }

    // Set socket options
    NetUtils::SetTcpNoDelay(tcpSocket, true);
    NetUtils::SetSocketTimeout(tcpSocket, 5000, 5000);

    // Setup server address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(config.tcpPort);
    inet_pton(AF_INET, config.serverHost.c_str(), &serverAddr.sin_addr);

    // Connect
    if (connect(tcpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        LOG_ERROR_FMT("Failed to connect to %s:%u: %d",
                     config.serverHost.c_str(), config.tcpPort,
                     NetUtils::GetLastSocketError());
        return false;
    }

    LOG_INFO_FMT("TCP connected to %s:%u", config.serverHost.c_str(), config.tcpPort);
    return true;
}

bool NetworkClient::CreateUDP() {
    // Create UDP socket
    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!NetUtils::IsValidSocket(udpSocket)) {
        LOG_ERROR_FMT("Failed to create UDP socket: %d", NetUtils::GetLastSocketError());
        return false;
    }

    // Bind to any port
    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port = 0; // Any port

    if (bind(udpSocket, (sockaddr*)&bindAddr, sizeof(bindAddr)) == SOCKET_ERROR) {
        LOG_ERROR_FMT("Failed to bind UDP socket: %d", NetUtils::GetLastSocketError());
        return false;
    }

    // Set large receive buffer
    NetUtils::SetSocketBufferSize(udpSocket, 2 * 1024 * 1024, 0); // 2MB receive buffer

    LOG_INFO("UDP socket created");
    return true;
}

bool NetworkClient::Handshake() {
    // Send handshake
    HandshakePacket handshake;
    handshake.clientVersion[0] = GUPT_VERSION.major;
    handshake.clientVersion[1] = GUPT_VERSION.minor;
    handshake.clientVersion[2] = GUPT_VERSION.patch;

    if (send(tcpSocket, (const char*)&handshake, sizeof(handshake), 0) != sizeof(handshake)) {
        LOG_ERROR("Failed to send handshake");
        return false;
    }

    // Receive response
    ControlPacket response;
    int bytesReceived = recv(tcpSocket, (char*)&response, sizeof(response), 0);
    if (bytesReceived != sizeof(response)) {
        LOG_ERROR("Failed to receive handshake response");
        return false;
    }

    if (!response.IsValid() || response.type != PACKET_TYPE_HANDSHAKE) {
        LOG_ERROR("Invalid handshake response");
        return false;
    }

    if (response.payload != 1) {
        LOG_ERROR("Handshake rejected by server");
        return false;
    }

    LOG_INFO("Handshake successful");
    return true;
}

bool NetworkClient::Authenticate() {
    // Send authentication
    AuthPacket auth;
    CryptoUtils::SHA256String(config.password, auth.passwordHash);

    if (send(tcpSocket, (const char*)&auth, sizeof(auth), 0) != sizeof(auth)) {
        LOG_ERROR("Failed to send authentication");
        return false;
    }

    // Receive response
    ControlPacket response;
    int bytesReceived = recv(tcpSocket, (char*)&response, sizeof(response), 0);
    if (bytesReceived != sizeof(response)) {
        LOG_ERROR("Failed to receive auth response");
        return false;
    }

    if (response.payload != 1) {
        LOG_ERROR("Authentication failed");
        return false;
    }

    LOG_INFO("Authentication successful");
    return true;
}

bool NetworkClient::Start() {
    if (running) {
        LOG_WARNING("Already running");
        return true;
    }

    LOG_INFO("Starting network client threads...");
    running = true;

    // Start TCP receive thread
    tcpReceiveThread = std::thread(&NetworkClient::TcpReceiveThread, this);

    // Start UDP receive thread
    udpReceiveThread = std::thread(&NetworkClient::UdpReceiveThread, this);

    // Start keepalive thread
    keepaliveThread = std::thread(&NetworkClient::KeepaliveThread, this);

    LOG_INFO("Network client threads started");
    return true;
}

void NetworkClient::Stop() {
    if (!running) {
        return;
    }

    LOG_INFO("Stopping network client threads...");
    running = false;

    // Wait for threads
    if (tcpReceiveThread.joinable()) {
        tcpReceiveThread.join();
    }
    if (udpReceiveThread.joinable()) {
        udpReceiveThread.join();
    }
    if (keepaliveThread.joinable()) {
        keepaliveThread.join();
    }

    LOG_INFO("Network client threads stopped");
}

void NetworkClient::TcpReceiveThread() {
    LOG_INFO("TCP receive thread started");

    while (running && connected) {
        ControlPacket packet;
        int bytesReceived = recv(tcpSocket, (char*)&packet, sizeof(packet), 0);

        if (bytesReceived <= 0) {
            int error = NetUtils::GetLastSocketError();
            if (error == WSAETIMEDOUT) {
                continue;
            }
            LOG_WARNING_FMT("TCP receive error: %d", error);
            connected = false;
            break;
        }

        if (bytesReceived != sizeof(ControlPacket)) {
            LOG_WARNING("Received invalid control packet size");
            continue;
        }

        if (!packet.IsValid()) {
            LOG_WARNING("Received invalid control packet");
            continue;
        }

        lastHeartbeat = PerformanceTimer::GetTimestampMicroseconds();
        HandleControlPacket(packet);
    }

    LOG_INFO("TCP receive thread stopped");
}

void NetworkClient::UdpReceiveThread() {
    LOG_INFO("UDP receive thread started");

    while (running && connected) {
        VideoPacket packet;
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);

        int bytesReceived = recvfrom(udpSocket, (char*)&packet, sizeof(packet), 0,
                                    (sockaddr*)&fromAddr, &fromLen);

        if (bytesReceived <= 0) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK || error == WSAETIMEDOUT) {
                continue;
            }
            LOG_WARNING_FMT("UDP receive error: %d", error);
            continue;
        }

        if (bytesReceived < (int)sizeof(VideoPacketHeader)) {
            continue;
        }

        if (!packet.IsValid()) {
            continue;
        }

        stats.packetsReceived++;
        stats.bytesReceived += packet.header.dataSize;

        // Assemble frame
        std::lock_guard<std::mutex> lock(assemblerMutex);
        auto& assembler = frameAssemblers[packet.header.frameId];

        if (assembler.AddPacket(packet)) {
            // Frame complete
            ReceivedFrame frame = assembler.GetFrame();
            stats.framesReceived++;

            // Calculate latency
            uint64_t now = PerformanceTimer::GetTimestampMicroseconds();
            stats.currentLatencyUs = now - frame.timestamp;

            // Add to jitter buffer
            jitterBuffer->Push(frame);

            // Remove from assemblers
            frameAssemblers.erase(packet.header.frameId);
        }

        // Clean up old incomplete frames
        uint64_t now = PerformanceTimer::GetTimestampMicroseconds();
        auto it = frameAssemblers.begin();
        while (it != frameAssemblers.end()) {
            if (it->second.IsExpired(now, Config::FRAME_TIMEOUT_US)) {
                stats.packetsLost += (it->second.totalPackets - it->second.receivedPackets);
                it = frameAssemblers.erase(it);
            } else {
                ++it;
            }
        }
    }

    LOG_INFO("UDP receive thread stopped");
}

void NetworkClient::KeepaliveThread() {
    LOG_INFO("Keepalive thread started");

    while (running && connected) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config.keepaliveIntervalMs));

        // Check timeout
        uint64_t now = PerformanceTimer::GetTimestampMicroseconds();
        if (now - lastHeartbeat > config.connectionTimeoutMs * 1000ULL) {
            LOG_WARNING("Server timeout");
            connected = false;
            break;
        }

        // Send keepalive
        SendKeepalive();
    }

    LOG_INFO("Keepalive thread stopped");
}

void NetworkClient::SendKeepalive() {
    ControlPacket packet;
    packet.type = PACKET_TYPE_KEEPALIVE;
    packet.timestamp = PerformanceTimer::GetTimestampMicroseconds();

    send(tcpSocket, (const char*)&packet, sizeof(packet), 0);
}

void NetworkClient::HandleControlPacket(const ControlPacket& packet) {
    switch (packet.type) {
        case PACKET_TYPE_KEEPALIVE:
            // Server keepalive received
            break;

        case PACKET_TYPE_DISCONNECT:
            LOG_INFO("Server requested disconnect");
            connected = false;
            break;

        default:
            LOG_DEBUG_FMT("Received control packet type: 0x%04X", packet.type);
            break;
    }
}

bool NetworkClient::GetNextFrame(ReceivedFrame& outFrame, uint32_t timeoutMs) {
    return jitterBuffer->Pop(outFrame, timeoutMs);
}

bool NetworkClient::SendInputEvent(const InputPacket& packet) {
    if (!connected) {
        return false;
    }

    int bytesSent = send(tcpSocket, (const char*)&packet, sizeof(packet), 0);
    return bytesSent == sizeof(packet);
}

// ============================================================================
// FrameAssembler Implementation
// ============================================================================

bool NetworkClient::FrameAssembler::AddPacket(const VideoPacket& packet) {
    if (totalPackets == 0) {
        frameId = packet.header.frameId;
        totalPackets = packet.header.totalPackets;
        isKeyframe = (packet.header.flags & FLAG_IDR_FRAME) != 0;
        firstPacketTime = PerformanceTimer::GetTimestampMicroseconds();
    }

    // Check for duplicate
    if (packets.find(packet.header.sequenceNum) != packets.end()) {
        return false;
    }

    // Store packet data
    packets[packet.header.sequenceNum].assign(
        packet.data,
        packet.data + packet.header.dataSize
    );
    receivedPackets++;

    return IsComplete();
}

ReceivedFrame NetworkClient::FrameAssembler::GetFrame() const {
    ReceivedFrame frame;
    frame.frameNumber = frameId;
    frame.timestamp = 0; // Will be set from first packet
    frame.isKeyframe = isKeyframe;
    frame.receiveTime = PerformanceTimer::GetTimestampMicroseconds();

    // Reassemble packets in order
    for (uint32_t i = 0; i < totalPackets; i++) {
        auto it = packets.find(i);
        if (it != packets.end()) {
            frame.data.insert(frame.data.end(), it->second.begin(), it->second.end());
        }
    }

    return frame;
}

bool NetworkClient::FrameAssembler::IsExpired(uint64_t now, uint64_t timeoutUs) const {
    return (now - firstPacketTime) > timeoutUs;
}

// ============================================================================
// ClientStats Implementation
// ============================================================================

void ClientStats::Print() const {
    std::cout << "\n=== Client Statistics ===\n";
    std::cout << "Frames received:   " << framesReceived.load() << "\n";
    std::cout << "Packets received:  " << packetsReceived.load() << "\n";
    std::cout << "Bytes received:    " << bytesReceived.load() << " (" << bytesReceived.load() / 1024 / 1024 << " MB)\n";
    std::cout << "Packets lost:      " << packetsLost.load() << "\n";
    std::cout << "Current FPS:       " << currentFps.load() << "\n";
    std::cout << "Current bitrate:   " << currentBitrate.load() / 1000000 << " Mbps\n";
    std::cout << "Current latency:   " << currentLatencyUs.load() / 1000 << " ms\n";
    std::cout << "========================\n\n";
}
