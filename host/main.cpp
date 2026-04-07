#include "capture.h"
#include "encoder.h"
#include "network_server.h"
#include "../common/utils.h"
#include "../common/logger.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>

// ============================================================================
// GuPT Host Application (Server)
// ============================================================================

std::atomic<bool> g_running(true);

void SignalHandler(int signal) {
    std::cout << "\n\nReceived signal " << signal << ", shutting down...\n";
    g_running = false;
}

class HostApplication {
public:
    HostApplication() {
        // Setup signal handlers
        signal(SIGINT, SignalHandler);
        signal(SIGTERM, SignalHandler);
    }

    bool Initialize() {
        std::cout << "=== GuPT Host Application ===\n";
        std::cout << "Version: " << (int)GUPT_VERSION.major << "."
                  << (int)GUPT_VERSION.minor << "."
                  << (int)GUPT_VERSION.patch << "\n\n";

        // Initialize logger
        Logger::GetInstance().SetLogLevel(LogLevel::INFO);
        Logger::GetInstance().SetLogToConsole(true);
        Logger::GetInstance().SetLogFile("gupt_host.log");

        LOG_INFO("Initializing host application...");

        // Initialize Winsock
        if (!NetUtils::InitializeWinsock()) {
            LOG_ERROR("Failed to initialize Winsock");
            return false;
        }

        // Initialize screen capture
        CaptureConfig captureConfig;
        captureConfig.adapterIndex = 0;
        captureConfig.outputIndex = 0;
        captureConfig.timeoutMs = 16; // ~60 FPS

        if (!screenCapture.Initialize(captureConfig)) {
            LOG_ERROR("Failed to initialize screen capture");
            NetUtils::CleanupWinsock();
            return false;
        }

        uint32_t width, height;
        screenCapture.GetDimensions(width, height);
        LOG_INFO_FMT("Screen capture initialized: %ux%u", width, height);

        // Initialize encoder
        EncoderConfig encConfig;
        encConfig.width = width;
        encConfig.height = height;
        encConfig.frameRate = 30;
        encConfig.bitrate = 5000000; // 5 Mbps

        encoder = EncoderFactory::CreateEncoder(screenCapture.GetDevice(), encConfig);
        if (!encoder) {
            LOG_ERROR("Failed to create encoder");
            screenCapture.Shutdown();
            NetUtils::CleanupWinsock();
            return false;
        }

        LOG_INFO_FMT("Encoder initialized: %s, %u Mbps",
                     encoder->GetType() == EncoderType::NVENC ? "NVENC" : "Software",
                     encConfig.bitrate / 1000000);

        // Initialize network server
        ServerConfig serverConfig;
        serverConfig.tcpPort = DEFAULT_TCP_PORT;
        serverConfig.udpPort = DEFAULT_UDP_PORT;
        serverConfig.maxClients = 1;
        serverConfig.requireAuth = false;

        if (!networkServer.Initialize(serverConfig)) {
            LOG_ERROR("Failed to initialize network server");
            encoder->Shutdown();
            screenCapture.Shutdown();
            NetUtils::CleanupWinsock();
            return false;
        }

        if (!networkServer.Start()) {
            LOG_ERROR("Failed to start network server");
            networkServer.Shutdown();
            encoder->Shutdown();
            screenCapture.Shutdown();
            NetUtils::CleanupWinsock();
            return false;
        }

        LOG_INFO_FMT("Network server started: TCP %u, UDP %u",
                     serverConfig.tcpPort, serverConfig.udpPort);

        // TODO: Initialize input handler (Phase 6)

        LOG_INFO("Host initialization complete");
        return true;
    }

    void Run() {
        LOG_INFO("Host application running. Press Ctrl+C to stop.");

        PerformanceMetrics metrics;
        FrameRateCalculator fpsCalc;

        while (g_running) {
            // Capture frame
            ID3D11Texture2D* texture = nullptr;
            FrameInfo frameInfo;

            {
                ScopedTimer timer(metrics.captureTimeUs);
                if (screenCapture.CaptureFrame(&texture, frameInfo)) {
                    if (frameInfo.hasFrameUpdate && texture) {
                        metrics.framesCaptured++;

                        // Encode frame
                        EncodedFrame encodedFrame;
                        {
                            ScopedTimer timer(metrics.encodeTimeUs);
                            if (encoder->EncodeFrame(texture, encodedFrame)) {
                                metrics.framesEncoded++;
                                fpsCalc.AddFrame();

                                // Send frame to clients
                                {
                                    ScopedTimer netTimer(metrics.networkTimeUs);
                                    if (networkServer.HasConnectedClients()) {
                                        if (networkServer.SendFrame(encodedFrame)) {
                                            metrics.framesSent++;
                                            metrics.bytesSent += encodedFrame.bitstreamSize;
                                        }
                                    }
                                }
                            }
                        }

                        screenCapture.ReleaseFrame();
                    }
                }
            }

            // Update FPS and log stats
            uint32_t fps = fpsCalc.CalculateFPS();
            if (fps > 0) {
                metrics.currentFps = fps;
                double avgCapture = metrics.framesCaptured > 0 ? (double)metrics.captureTimeUs / metrics.framesCaptured / 1000.0 : 0;
                double avgEncode = metrics.framesEncoded > 0 ? (double)metrics.encodeTimeUs / metrics.framesEncoded / 1000.0 : 0;
                double actualBitrate = metrics.framesEncoded > 0 ? (double)metrics.bytesSent * 8 * 30 / metrics.framesEncoded / 1000000.0 : 0;

                double avgNetwork = metrics.framesSent > 0 ? (double)metrics.networkTimeUs / metrics.framesSent / 1000.0 : 0;
                size_t clientCount = networkServer.GetClientCount();

                LOG_INFO_FMT("FPS: %u, Frames: %u, Clients: %zu, Capture: %.1f ms, Encode: %.1f ms, Network: %.1f ms, Bitrate: %.1f Mbps",
                            fps, metrics.framesEncoded.load(), clientCount, avgCapture, avgEncode, avgNetwork, actualBitrate);
            }

            // Target 60 FPS
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        LOG_INFO("Shutting down host application...");
        metrics.Print();
    }

    void Shutdown() {
        networkServer.Shutdown();
        if (encoder) {
            encoder->Shutdown();
        }
        screenCapture.Shutdown();
        NetUtils::CleanupWinsock();

        // Print final stats
        LOG_INFO("=== Final Statistics ===");
        networkServer.GetStats().Print();

        LOG_INFO("Host application shutdown complete");
    }

private:
    ScreenCapture screenCapture;
    std::unique_ptr<VideoEncoder> encoder;
    NetworkServer networkServer;
    // TODO: Add input handler
};

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    // TODO: Parse command-line arguments
    // --port <port>
    // --signaling <url>
    // --session <id>
    // --password <password>
    // --adapter <index>
    // --output <index>

    HostApplication app;

    if (!app.Initialize()) {
        std::cerr << "Failed to initialize host application\n";
        return 1;
    }

    app.Run();
    app.Shutdown();

    return 0;
}
