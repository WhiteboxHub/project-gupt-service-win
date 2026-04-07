#include "network_client.h"
#include "decoder.h"
#include "renderer.h"
#include "input_sender.h"
#include "../common/utils.h"
#include <iostream>
#include <csignal>
#include <atomic>

// ============================================================================
// Client Application - Main Entry Point
// ============================================================================

std::atomic<bool> g_running(true);

void SignalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down...\n";
    g_running = false;
}

void PrintUsage(const char* programName) {
    std::cout << "Gupt Client - Remote Desktop Client\n";
    std::cout << "Usage: " << programName << " [options]\n";
    std::cout << "\nOptions:\n";
    std::cout << "  -h, --host <ip>        Server host (default: 127.0.0.1)\n";
    std::cout << "  -p, --port <port>      Server TCP port (default: 5900)\n";
    std::cout << "  -P, --password <pass>  Authentication password\n";
    std::cout << "  -w, --width <width>    Window width (default: 1920)\n";
    std::cout << "  -H, --height <height>  Window height (default: 1080)\n";
    std::cout << "  --no-input             Disable input capture\n";
    std::cout << "  --help                 Show this help\n";
    std::cout << "\nExample:\n";
    std::cout << "  " << programName << " --host 192.168.1.100 --password mypass\n\n";
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    ClientConfig clientConfig;
    DecoderConfig decoderConfig;
    InputSenderConfig inputConfig;
    bool enableInput = true;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else if ((arg == "-h" || arg == "--host") && i + 1 < argc) {
            clientConfig.serverHost = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            clientConfig.tcpPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if ((arg == "-P" || arg == "--password") && i + 1 < argc) {
            clientConfig.password = argv[++i];
        } else if ((arg == "-w" || arg == "--width") && i + 1 < argc) {
            decoderConfig.width = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if ((arg == "-H" || arg == "--height") && i + 1 < argc) {
            decoderConfig.height = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "--no-input") {
            enableInput = false;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    // Install signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    std::cout << "\n";
    std::cout << "================================================\n";
    std::cout << "  Gupt Client - Remote Desktop Client\n";
    std::cout << "  Version " << GUPT_VERSION.major << "." << GUPT_VERSION.minor << "." << GUPT_VERSION.patch << "\n";
    std::cout << "================================================\n\n";

    // Initialize logger
    Logger::SetLevel(LogLevel::Info);

    LOG_INFO("Starting Gupt Client...");
    LOG_INFO_FMT("Connecting to %s:%u", clientConfig.serverHost.c_str(), clientConfig.tcpPort);

    // ========================================================================
    // Create Components
    // ========================================================================

    // Network client
    NetworkClient networkClient;
    if (!networkClient.Connect(clientConfig)) {
        LOG_ERROR("Failed to connect to server");
        std::cout << "\nPress Enter to exit...\n";
        std::cin.get();
        return 1;
    }

    if (!networkClient.Start()) {
        LOG_ERROR("Failed to start network client");
        networkClient.Disconnect();
        std::cout << "\nPress Enter to exit...\n";
        std::cin.get();
        return 1;
    }

    LOG_INFO("Connected to server");

    // Decoder
    auto decoder = DecoderFactory::CreateDecoder(nullptr, decoderConfig);
    if (!decoder) {
        LOG_ERROR("Failed to create decoder");
        networkClient.Disconnect();
        std::cout << "\nPress Enter to exit...\n";
        std::cin.get();
        return 1;
    }

    // Renderer
    D3DRenderer renderer;
    std::wstring windowTitle = L"Gupt Client - " + 
        std::wstring(clientConfig.serverHost.begin(), clientConfig.serverHost.end());
    
    if (!renderer.Initialize(windowTitle.c_str(), decoderConfig.width, decoderConfig.height)) {
        LOG_ERROR("Failed to initialize renderer");
        networkClient.Disconnect();
        std::cout << "\nPress Enter to exit...\n";
        std::cin.get();
        return 1;
    }

    // Re-initialize decoder with renderer's D3D device for zero-copy
    decoder = DecoderFactory::CreateDecoder(renderer.GetDevice(), decoderConfig);
    if (!decoder) {
        LOG_ERROR("Failed to re-create decoder with shared device");
        renderer.Shutdown();
        networkClient.Disconnect();
        std::cout << "\nPress Enter to exit...\n";
        std::cin.get();
        return 1;
    }

    LOG_INFO("Renderer initialized");

    // Input sender
    std::unique_ptr<InputSender> inputSender;
    if (enableInput) {
        inputSender = std::make_unique<InputSender>();
        if (!inputSender->Initialize(renderer.GetWindowHandle(), &networkClient, inputConfig)) {
            LOG_WARNING("Failed to initialize input sender, continuing without input");
            inputSender.reset();
        } else {
            if (!inputSender->Start()) {
                LOG_WARNING("Failed to start input sender, continuing without input");
                inputSender.reset();
            } else {
                LOG_INFO("Input capture enabled");
            }
        }
    } else {
        LOG_INFO("Input capture disabled");
    }

    // ========================================================================
    // Main Loop
    // ========================================================================

    LOG_INFO("Entering main loop...");
    std::cout << "\n=== Client Running ===\n";
    std::cout << "Press Ctrl+C to stop, or close the window\n\n";

    FrameRateCalculator fpsCalc;
    uint64_t frameCount = 0;
    uint64_t lastStatsTime = PerformanceTimer::GetTimestampMicroseconds();

    while (g_running && networkClient.IsConnected()) {
        // Process window messages
        if (!renderer.ProcessMessages()) {
            LOG_INFO("Window closed by user");
            break;
        }

        // Get next frame from network
        ReceivedFrame receivedFrame;
        if (networkClient.GetNextFrame(receivedFrame, 100)) {
            // Decode frame
            DecodedFrame decodedFrame;
            if (decoder->DecodeFrame(receivedFrame, decodedFrame)) {
                // Render frame
                if (renderer.RenderFrame(decodedFrame.texture)) {
                    renderer.Present();
                    
                    frameCount++;
                    fpsCalc.RecordFrame();
                }
            }
        }

        // Print statistics every 2 seconds
        uint64_t now = PerformanceTimer::GetTimestampMicroseconds();
        if (now - lastStatsTime > 2000000) {
            const ClientStats& stats = networkClient.GetStats();
            
            std::cout << "\r";
            std::cout << "FPS: " << fpsCalc.GetFPS();
            std::cout << " | Frames: " << stats.framesReceived.load();
            std::cout << " | Packets: " << stats.packetsReceived.load();
            std::cout << " | Lost: " << stats.packetsLost.load();
            std::cout << " | Latency: " << stats.currentLatencyUs.load() / 1000 << "ms";
            std::cout << " | Bitrate: " << stats.currentBitrate.load() / 1000000 << "Mbps";
            std::cout << "        ";
            std::cout.flush();

            lastStatsTime = now;
        }

        // Small sleep to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // ========================================================================
    // Cleanup
    // ========================================================================

    LOG_INFO("Shutting down...");
    std::cout << "\n\nShutting down...\n";

    if (inputSender) {
        inputSender->Shutdown();
    }

    renderer.Shutdown();
    networkClient.Disconnect();

    // Print final statistics
    std::cout << "\n";
    networkClient.GetStats().Print();

    LOG_INFO("Client shutdown complete");
    std::cout << "Client shutdown complete\n";

    return 0;
}
