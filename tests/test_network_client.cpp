#include "../common/protocol.h"
#include "../common/packet.h"
#include "../common/utils.h"
#include "../common/logger.h"
#include <iostream>
#include <fstream>
#include <map>
#include <conio.h>

// ============================================================================
// Simple Network Client for Testing
// ============================================================================

class TestNetworkClient {
public:
    TestNetworkClient() : tcpSocket(INVALID_SOCKET), udpSocket(INVALID_SOCKET), running(false) {}

    ~TestNetworkClient() {
        Disconnect();
    }

    bool Connect(const char* host, uint16_t tcpPort, uint16_t udpPort) {
        LOG_INFO_FMT("Connecting to %s:%u (TCP) and %u (UDP)...", host, tcpPort, udpPort);

        // Create TCP socket
        tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (!NetUtils::IsValidSocket(tcpSocket)) {
            LOG_ERROR("Failed to create TCP socket");
            return false;
        }

        // Connect TCP
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(tcpPort);
        inet_pton(AF_INET, host, &serverAddr.sin_addr);

        if (connect(tcpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            LOG_ERROR_FMT("Failed to connect TCP: %d", NetUtils::GetLastSocketError());
            NetUtils::CloseSocket(tcpSocket);
            return false;
        }

        LOG_INFO("TCP connected");

        // Send handshake
        if (!SendHandshake()) {
            LOG_ERROR("Handshake failed");
            NetUtils::CloseSocket(tcpSocket);
            return false;
        }

        // Create UDP socket
        udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (!NetUtils::IsValidSocket(udpSocket)) {
            LOG_ERROR("Failed to create UDP socket");
            NetUtils::CloseSocket(tcpSocket);
            return false;
        }

        // Set UDP server address
        udpServerAddr = serverAddr;
        udpServerAddr.sin_port = htons(udpPort);

        // Bind UDP to any port
        sockaddr_in udpBindAddr{};
        udpBindAddr.sin_family = AF_INET;
        udpBindAddr.sin_addr.s_addr = INADDR_ANY;
        udpBindAddr.sin_port = 0; // Any port

        if (bind(udpSocket, (sockaddr*)&udpBindAddr, sizeof(udpBindAddr)) == SOCKET_ERROR) {
            LOG_ERROR_FMT("Failed to bind UDP: %d", NetUtils::GetLastSocketError());
            NetUtils::CloseSocket(udpSocket);
            NetUtils::CloseSocket(tcpSocket);
            return false;
        }

        LOG_INFO("UDP socket created");

        running = true;
        return true;
    }

    void Disconnect() {
        running = false;
        NetUtils::CloseSocket(tcpSocket);
        NetUtils::CloseSocket(udpSocket);
        LOG_INFO("Disconnected");
    }

    void ReceiveFrames(const char* outputFile = nullptr) {
        std::cout << "Receiving frames... Press 'q' to stop.\n\n";

        std::ofstream outFile;
        if (outputFile) {
            outFile.open(outputFile, std::ios::binary);
            if (!outFile) {
                LOG_WARNING_FMT("Failed to open output file: %s", outputFile);
            } else {
                LOG_INFO_FMT("Saving stream to: %s", outputFile);
            }
        }

        std::map<uint64_t, FrameAssembler> frameAssemblers;
        uint64_t framesReceived = 0;
        uint64_t packetsReceived = 0;
        uint64_t bytesReceived = 0;

        PerformanceTimer timer;
        FrameRateCalculator fpsCalc;

        while (running) {
            // Check for keyboard input
            if (_kbhit()) {
                char ch = _getch();
                if (ch == 'q' || ch == 'Q') {
                    break;
                }
            }

            // Receive UDP packet
            VideoPacket packet;
            sockaddr_in fromAddr;
            int fromLen = sizeof(fromAddr);

            int bytesRecv = recvfrom(udpSocket, (char*)&packet, sizeof(packet), 0,
                                    (sockaddr*)&fromAddr, &fromLen);

            if (bytesRecv <= 0) {
                int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK) {
                    continue; // No data
                }
                LOG_ERROR_FMT("UDP receive error: %d", error);
                break;
            }

            if (bytesRecv < (int)sizeof(VideoPacketHeader)) {
                LOG_WARNING("Received incomplete packet");
                continue;
            }

            if (!packet.IsValid()) {
                LOG_WARNING("Received invalid packet");
                continue;
            }

            packetsReceived++;
            bytesReceived += packet.header.dataSize;

            // Assemble frame
            auto& assembler = frameAssemblers[packet.header.frameId];
            if (assembler.AddPacket(packet)) {
                // Frame complete
                framesReceived++;
                fpsCalc.AddFrame();

                std::cout << "Frame " << packet.header.frameId
                          << ": " << assembler.GetFrameSize() << " bytes"
                          << (assembler.IsKeyframe() ? " [KEYFRAME]" : "")
                          << "\n";

                // Save to file
                if (outFile.is_open()) {
                    auto& data = assembler.GetData();
                    outFile.write((const char*)data.data(), data.size());
                }

                frameAssemblers.erase(packet.header.frameId);
            }

            // Calculate FPS
            uint32_t fps = fpsCalc.CalculateFPS();
            if (fps > 0) {
                double elapsed = timer.ElapsedSeconds();
                double bitrate = (bytesReceived * 8.0) / elapsed / 1000000.0;

                std::cout << "\rFPS: " << fps
                          << " | Frames: " << framesReceived
                          << " | Packets: " << packetsReceived
                          << " | Bitrate: " << bitrate << " Mbps"
                          << "                    ";
                std::cout.flush();
            }

            // Clean up old incomplete frames
            auto it = frameAssemblers.begin();
            while (it != frameAssemblers.end()) {
                if (it->first < packet.header.frameId - 10) { // Keep only recent 10 frames
                    it = frameAssemblers.erase(it);
                } else {
                    ++it;
                }
            }
        }

        if (outFile.is_open()) {
            outFile.close();
            LOG_INFO("Stream saved");
        }

        std::cout << "\n\nFinal Statistics:\n";
        std::cout << "  Frames received: " << framesReceived << "\n";
        std::cout << "  Packets received: " << packetsReceived << "\n";
        std::cout << "  Bytes received: " << bytesReceived << " (" << bytesReceived / 1024 / 1024 << " MB)\n";
        double elapsed = timer.ElapsedSeconds();
        std::cout << "  Time: " << elapsed << " seconds\n";
        std::cout << "  Average FPS: " << (elapsed > 0 ? framesReceived / elapsed : 0) << "\n";
        std::cout << "  Average bitrate: " << (elapsed > 0 ? (bytesReceived * 8.0) / elapsed / 1000000.0 : 0) << " Mbps\n";
    }

private:
    bool SendHandshake() {
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
        if (recv(tcpSocket, (char*)&response, sizeof(response), 0) != sizeof(response)) {
            LOG_ERROR("Failed to receive handshake response");
            return false;
        }

        if (response.type != PACKET_TYPE_HANDSHAKE || response.payload != 1) {
            LOG_ERROR("Handshake rejected");
            return false;
        }

        LOG_INFO("Handshake successful");
        return true;
    }

    struct FrameAssembler {
        uint64_t frameId = 0;
        uint32_t totalPackets = 0;
        uint32_t receivedPackets = 0;
        bool isKeyframe = false;
        std::map<uint32_t, std::vector<uint8_t>> packets;

        bool AddPacket(const VideoPacket& packet) {
            if (totalPackets == 0) {
                frameId = packet.header.frameId;
                totalPackets = packet.header.totalPackets;
                isKeyframe = (packet.header.flags & FLAG_IDR_FRAME) != 0;
            }

            if (packets.find(packet.header.sequenceNum) != packets.end()) {
                return false; // Duplicate
            }

            packets[packet.header.sequenceNum].assign(
                packet.data,
                packet.data + packet.header.dataSize
            );
            receivedPackets++;

            return receivedPackets == totalPackets;
        }

        std::vector<uint8_t> GetData() const {
            std::vector<uint8_t> result;
            for (uint32_t i = 0; i < totalPackets; i++) {
                auto it = packets.find(i);
                if (it != packets.end()) {
                    result.insert(result.end(), it->second.begin(), it->second.end());
                }
            }
            return result;
        }

        size_t GetFrameSize() const {
            size_t size = 0;
            for (const auto& p : packets) {
                size += p.second.size();
            }
            return size;
        }

        bool IsKeyframe() const { return isKeyframe; }
    };

    socket_t tcpSocket;
    socket_t udpSocket;
    sockaddr_in udpServerAddr;
    std::atomic<bool> running;
};

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    // Initialize logger
    Logger::GetInstance().SetLogLevel(LogLevel::INFO);
    Logger::GetInstance().SetLogToConsole(true);

    // Initialize Winsock
    if (!NetUtils::InitializeWinsock()) {
        std::cerr << "Failed to initialize Winsock\n";
        return 1;
    }

    // Parse arguments
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    uint16_t tcpPort = (argc > 2) ? (uint16_t)atoi(argv[2]) : DEFAULT_TCP_PORT;
    uint16_t udpPort = (argc > 3) ? (uint16_t)atoi(argv[3]) : DEFAULT_UDP_PORT;
    const char* outputFile = (argc > 4) ? argv[4] : "received_stream.h264";

    std::cout << "=== GuPT Network Test Client ===\n";
    std::cout << "Host: " << host << "\n";
    std::cout << "TCP Port: " << tcpPort << "\n";
    std::cout << "UDP Port: " << udpPort << "\n";
    std::cout << "Output: " << outputFile << "\n\n";

    TestNetworkClient client;

    if (!client.Connect(host, tcpPort, udpPort)) {
        std::cerr << "Failed to connect to server\n";
        NetUtils::CleanupWinsock();
        return 1;
    }

    client.ReceiveFrames(outputFile);
    client.Disconnect();

    NetUtils::CleanupWinsock();

    std::cout << "\nGoodbye!\n";
    return 0;
}
