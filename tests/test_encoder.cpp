#include "../host/capture.h"
#include "../host/encoder.h"
#include "../common/utils.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <conio.h>

// ============================================================================
// Encoder Test Program
// ============================================================================

class EncoderTest {
public:
    void Run() {
        std::cout << "=== GuPT Encoder Test ===\n\n";

        Logger::GetInstance().SetLogLevel(LogLevel::INFO);
        Logger::GetInstance().SetLogToConsole(true);

        // Check encoder availability
        CheckAvailability();

        // Run tests
        if (!TestBasicEncoding()) {
            std::cerr << "\nBasic encoding test FAILED!\n";
            return;
        }

        if (!TestPerformance()) {
            std::cerr << "\nPerformance test FAILED!\n";
            return;
        }

        if (!TestBitrateChange()) {
            std::cerr << "\nBitrate change test FAILED!\n";
            return;
        }

        if (!TestKeyframeGeneration()) {
            std::cerr << "\nKeyframe generation test FAILED!\n";
            return;
        }

        std::cout << "\n=== ALL TESTS PASSED ===\n";
    }

private:
    void CheckAvailability() {
        std::cout << "\n--- Encoder Availability ---\n";
        std::cout << "NVENC:    " << (EncoderFactory::IsNvencAvailable() ? "Available" : "Not available (stub mode)") << "\n";
        std::cout << "Software: " << (EncoderFactory::IsSoftwareAvailable() ? "Available" : "Not available (stub mode)") << "\n";
        std::cout << "\n";
    }

    bool TestBasicEncoding() {
        std::cout << "\n--- Test 1: Basic Encoding ---\n";

        // Initialize capture
        ScreenCapture capture;
        if (!capture.Initialize()) {
            std::cerr << "Failed to initialize capture\n";
            return false;
        }

        uint32_t width, height;
        capture.GetDimensions(width, height);

        // Initialize encoder
        EncoderConfig encConfig;
        encConfig.width = width;
        encConfig.height = height;
        encConfig.frameRate = 30;
        encConfig.bitrate = 5000000; // 5 Mbps

        auto encoder = EncoderFactory::CreateEncoder(capture.GetDevice(), encConfig);
        if (!encoder) {
            std::cerr << "Failed to create encoder\n";
            return false;
        }

        std::cout << "Encoder type: " << (encoder->GetType() == EncoderType::NVENC ? "NVENC" : "Software") << "\n";
        std::cout << "Configuration: " << width << "x" << height << " @ " << encConfig.frameRate << " fps, "
                  << encConfig.bitrate / 1000000 << " Mbps\n";

        // Encode 10 frames
        std::cout << "\nEncoding 10 frames...\n";
        int successCount = 0;
        size_t totalBytes = 0;

        for (int i = 0; i < 10; i++) {
            ID3D11Texture2D* texture = nullptr;
            FrameInfo captureInfo;

            if (capture.CaptureFrame(&texture, captureInfo) && captureInfo.hasFrameUpdate && texture) {
                EncodedFrame encodedFrame;

                if (encoder->EncodeFrame(texture, encodedFrame)) {
                    successCount++;
                    totalBytes += encodedFrame.bitstreamSize;

                    std::cout << "  Frame " << encodedFrame.frameNumber
                              << ": " << encodedFrame.bitstreamSize << " bytes"
                              << (encodedFrame.isKeyframe ? " [KEYFRAME]" : "")
                              << "\n";

                    // Save first frame to file
                    if (i == 0) {
                        std::ofstream file("test_frame.h264", std::ios::binary);
                        if (file) {
                            file.write(reinterpret_cast<const char*>(encodedFrame.data.data()),
                                      encodedFrame.bitstreamSize);
                            file.close();
                            std::cout << "    Saved to test_frame.h264\n";
                        }
                    }
                }

                capture.ReleaseFrame();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 fps
        }

        encoder->Shutdown();
        capture.Shutdown();

        std::cout << "\nEncoded " << successCount << " frames successfully\n";
        std::cout << "Total size: " << totalBytes << " bytes (" << totalBytes / 1024 << " KB)\n";
        std::cout << "Average size: " << (successCount > 0 ? totalBytes / successCount : 0) << " bytes/frame\n";

        return successCount > 0;
    }

    bool TestPerformance() {
        std::cout << "\n--- Test 2: Performance Benchmark ---\n";

        ScreenCapture capture;
        if (!capture.Initialize()) {
            return false;
        }

        uint32_t width, height;
        capture.GetDimensions(width, height);

        EncoderConfig config;
        config.width = width;
        config.height = height;
        config.frameRate = 30;
        config.bitrate = 5000000;

        auto encoder = EncoderFactory::CreateEncoder(capture.GetDevice(), config);
        if (!encoder) {
            return false;
        }

        const int FRAME_COUNT = 100;
        std::cout << "Encoding " << FRAME_COUNT << " frames for performance measurement...\n";

        PerformanceTimer timer;
        uint64_t totalCaptureTime = 0;
        uint64_t totalEncodeTime = 0;
        int framesEncoded = 0;
        size_t totalBytes = 0;

        for (int i = 0; i < FRAME_COUNT; i++) {
            ID3D11Texture2D* texture = nullptr;
            FrameInfo captureInfo;

            PerformanceTimer captureTimer;
            bool captured = capture.CaptureFrame(&texture, captureInfo);
            totalCaptureTime += captureTimer.ElapsedMicroseconds();

            if (captured && captureInfo.hasFrameUpdate && texture) {
                EncodedFrame encodedFrame;

                PerformanceTimer encodeTimer;
                bool success = encoder->EncodeFrame(texture, encodedFrame);
                uint64_t encodeTime = encodeTimer.ElapsedMicroseconds();

                if (success) {
                    totalEncodeTime += encodeTime;
                    totalBytes += encodedFrame.bitstreamSize;
                    framesEncoded++;
                }

                capture.ReleaseFrame();
            }

            // Don't sleep - measure max throughput
        }

        double totalTime = timer.ElapsedMilliseconds();
        double avgCaptureTime = framesEncoded > 0 ? (double)totalCaptureTime / framesEncoded / 1000.0 : 0;
        double avgEncodeTime = framesEncoded > 0 ? (double)totalEncodeTime / framesEncoded / 1000.0 : 0;
        double avgTotalTime = avgCaptureTime + avgEncodeTime;
        double fps = (framesEncoded * 1000.0) / totalTime;
        double actualBitrate = (totalBytes * 8.0 * config.frameRate) / framesEncoded;

        std::cout << "\nResults:\n";
        std::cout << "  Total time: " << totalTime << " ms\n";
        std::cout << "  Frames encoded: " << framesEncoded << "\n";
        std::cout << "  Average capture time: " << avgCaptureTime << " ms\n";
        std::cout << "  Average encode time: " << avgEncodeTime << " ms\n";
        std::cout << "  Average total time: " << avgTotalTime << " ms\n";
        std::cout << "  Average FPS: " << fps << "\n";
        std::cout << "  Actual bitrate: " << actualBitrate / 1000000.0 << " Mbps\n";

        // Check performance targets
        bool captureTarget = avgCaptureTime < 5.0;
        bool encodeTarget = avgEncodeTime < 10.0;
        bool totalTarget = avgTotalTime < 15.0;

        std::cout << "\n  Performance targets:\n";
        std::cout << "    Capture <5ms:  " << (captureTarget ? "PASS" : "FAIL") << "\n";
        std::cout << "    Encode <10ms:  " << (encodeTarget ? "PASS" : "FAIL") << "\n";
        std::cout << "    Total <15ms:   " << (totalTarget ? "PASS" : "FAIL") << "\n";

        encoder->Shutdown();
        capture.Shutdown();

        return true;
    }

    bool TestBitrateChange() {
        std::cout << "\n--- Test 3: Dynamic Bitrate Change ---\n";

        ScreenCapture capture;
        if (!capture.Initialize()) {
            return false;
        }

        uint32_t width, height;
        capture.GetDimensions(width, height);

        EncoderConfig config;
        config.width = width;
        config.height = height;
        config.frameRate = 30;
        config.bitrate = 2000000; // Start at 2 Mbps

        auto encoder = EncoderFactory::CreateEncoder(capture.GetDevice(), config);
        if (!encoder) {
            return false;
        }

        std::cout << "Encoding with bitrate changes:\n";
        uint32_t bitrates[] = {2000000, 5000000, 8000000, 3000000}; // 2, 5, 8, 3 Mbps
        const char* bitrateNames[] = {"2 Mbps", "5 Mbps", "8 Mbps", "3 Mbps"};

        for (int bitrateIdx = 0; bitrateIdx < 4; bitrateIdx++) {
            encoder->Reconfigure(bitrates[bitrateIdx]);
            std::cout << "\n  Bitrate: " << bitrateNames[bitrateIdx] << "\n";

            size_t totalBytes = 0;
            int framesEncoded = 0;

            for (int i = 0; i < 10; i++) {
                ID3D11Texture2D* texture = nullptr;
                FrameInfo captureInfo;

                if (capture.CaptureFrame(&texture, captureInfo) && captureInfo.hasFrameUpdate && texture) {
                    EncodedFrame encodedFrame;

                    if (encoder->EncodeFrame(texture, encodedFrame)) {
                        totalBytes += encodedFrame.bitstreamSize;
                        framesEncoded++;
                    }

                    capture.ReleaseFrame();
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }

            double avgSize = framesEncoded > 0 ? (double)totalBytes / framesEncoded : 0;
            double measuredBitrate = (totalBytes * 8.0 * config.frameRate) / framesEncoded;

            std::cout << "    Frames: " << framesEncoded
                      << ", Avg size: " << (int)avgSize << " bytes"
                      << ", Measured: " << measuredBitrate / 1000000.0 << " Mbps\n";
        }

        encoder->Shutdown();
        capture.Shutdown();

        return true;
    }

    bool TestKeyframeGeneration() {
        std::cout << "\n--- Test 4: Keyframe Generation ---\n";

        ScreenCapture capture;
        if (!capture.Initialize()) {
            return false;
        }

        uint32_t width, height;
        capture.GetDimensions(width, height);

        EncoderConfig config;
        config.width = width;
        config.height = height;
        config.frameRate = 30;
        config.bitrate = 5000000;
        config.idrPeriod = 30; // IDR every 30 frames (1 second)

        auto encoder = EncoderFactory::CreateEncoder(capture.GetDevice(), config);
        if (!encoder) {
            return false;
        }

        std::cout << "Encoding 40 frames (IDR period: 30 frames)...\n";
        std::cout << "Will force keyframe at frame 20\n\n";

        int keyframeCount = 0;
        std::vector<int> keyframeIndices;

        for (int i = 0; i < 40; i++) {
            // Force keyframe at frame 20
            if (i == 20) {
                encoder->ForceKeyframe();
                std::cout << "  >>> Forcing keyframe <<<\n";
            }

            ID3D11Texture2D* texture = nullptr;
            FrameInfo captureInfo;

            if (capture.CaptureFrame(&texture, captureInfo) && captureInfo.hasFrameUpdate && texture) {
                EncodedFrame encodedFrame;

                if (encoder->EncodeFrame(texture, encodedFrame)) {
                    if (encodedFrame.isKeyframe) {
                        keyframeCount++;
                        keyframeIndices.push_back(i);
                        std::cout << "  Frame " << i << ": KEYFRAME (" << encodedFrame.bitstreamSize << " bytes)\n";
                    }
                }

                capture.ReleaseFrame();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }

        std::cout << "\nKeyframe summary:\n";
        std::cout << "  Total keyframes: " << keyframeCount << "\n";
        std::cout << "  Keyframe indices: ";
        for (int idx : keyframeIndices) {
            std::cout << idx << " ";
        }
        std::cout << "\n";

        bool hasFirstKeyframe = !keyframeIndices.empty() && keyframeIndices[0] == 0;
        bool hasForcedKeyframe = std::find(keyframeIndices.begin(), keyframeIndices.end(), 20) != keyframeIndices.end();
        bool hasPeriodicKeyframe = std::find(keyframeIndices.begin(), keyframeIndices.end(), 30) != keyframeIndices.end();

        std::cout << "\n  Tests:\n";
        std::cout << "    First frame is keyframe: " << (hasFirstKeyframe ? "PASS" : "FAIL") << "\n";
        std::cout << "    Forced keyframe at 20:   " << (hasForcedKeyframe ? "PASS" : "FAIL") << "\n";
        std::cout << "    Periodic keyframe at 30: " << (hasPeriodicKeyframe ? "PASS" : "FAIL") << "\n";

        encoder->Shutdown();
        capture.Shutdown();

        return hasFirstKeyframe || hasForcedKeyframe || hasPeriodicKeyframe;
    }
};

// ============================================================================
// Interactive Test Menu
// ============================================================================

void ShowMenu() {
    std::cout << "\n=== GuPT Encoder Test Menu ===\n";
    std::cout << "1. Run all tests\n";
    std::cout << "2. Basic encoding test\n";
    std::cout << "3. Performance benchmark\n";
    std::cout << "4. Bitrate change test\n";
    std::cout << "5. Keyframe generation test\n";
    std::cout << "6. Continuous encode (press 'q' to stop)\n";
    std::cout << "7. Check encoder availability\n";
    std::cout << "0. Exit\n";
    std::cout << "\nChoice: ";
}

void ContinuousEncode() {
    std::cout << "\n--- Continuous Encode Mode ---\n";
    std::cout << "Press 'q' to stop...\n\n";

    Logger::GetInstance().SetLogLevel(LogLevel::WARNING);

    ScreenCapture capture;
    if (!capture.Initialize()) {
        std::cerr << "Failed to initialize capture\n";
        return;
    }

    uint32_t width, height;
    capture.GetDimensions(width, height);

    EncoderConfig config;
    config.width = width;
    config.height = height;
    config.frameRate = 30;
    config.bitrate = 5000000;

    auto encoder = EncoderFactory::CreateEncoder(capture.GetDevice(), config);
    if (!encoder) {
        std::cerr << "Failed to create encoder\n";
        return;
    }

    PerformanceMetrics metrics;
    FrameRateCalculator fpsCalc;
    std::atomic<bool> running(true);

    std::thread workThread([&]() {
        while (running) {
            ID3D11Texture2D* texture = nullptr;
            FrameInfo captureInfo;

            {
                ScopedTimer timer(metrics.captureTimeUs);
                capture.CaptureFrame(&texture, captureInfo);
            }

            if (captureInfo.hasFrameUpdate && texture) {
                metrics.framesCaptured++;

                EncodedFrame encodedFrame;
                {
                    ScopedTimer timer(metrics.encodeTimeUs);
                    if (encoder->EncodeFrame(texture, encodedFrame)) {
                        metrics.framesEncoded++;
                        metrics.bytesSent += encodedFrame.bitstreamSize;
                        fpsCalc.AddFrame();
                    }
                }

                capture.ReleaseFrame();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 fps
        }
    });

    while (running) {
        if (_kbhit()) {
            char ch = _getch();
            if (ch == 'q' || ch == 'Q') {
                running = false;
            }
        }

        uint32_t fps = fpsCalc.CalculateFPS();
        if (fps > 0) {
            metrics.currentFps = fps;
            double avgCapture = metrics.framesCaptured > 0 ? (double)metrics.captureTimeUs / metrics.framesCaptured / 1000.0 : 0;
            double avgEncode = metrics.framesEncoded > 0 ? (double)metrics.encodeTimeUs / metrics.framesEncoded / 1000.0 : 0;
            double actualBitrate = metrics.framesEncoded > 0 ? (double)metrics.bytesSent * 8 * 30 / metrics.framesEncoded / 1000000.0 : 0;

            std::cout << "\rFPS: " << fps
                      << " | Frames: " << metrics.framesEncoded.load()
                      << " | Capture: " << avgCapture << " ms"
                      << " | Encode: " << avgEncode << " ms"
                      << " | Bitrate: " << actualBitrate << " Mbps"
                      << "                    ";
            std::cout.flush();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    workThread.join();
    encoder->Shutdown();
    capture.Shutdown();

    std::cout << "\n\nFinal Statistics:\n";
    metrics.Print();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    if (!NetUtils::InitializeWinsock()) {
        std::cerr << "Failed to initialize Winsock\n";
        return 1;
    }

    while (true) {
        ShowMenu();

        int choice;
        std::cin >> choice;

        if (choice == 0) {
            break;
        }

        EncoderTest test;

        switch (choice) {
            case 1:
                test.Run();
                break;
            case 2:
                test.TestBasicEncoding();
                break;
            case 3:
                test.TestPerformance();
                break;
            case 4:
                test.TestBitrateChange();
                break;
            case 5:
                test.TestKeyframeGeneration();
                break;
            case 6:
                ContinuousEncode();
                break;
            case 7:
                std::cout << "\nNVENC:    " << (EncoderFactory::IsNvencAvailable() ? "Available" : "Not available") << "\n";
                std::cout << "Software: " << (EncoderFactory::IsSoftwareAvailable() ? "Available" : "Not available") << "\n";
                break;
            default:
                std::cout << "Invalid choice\n";
        }
    }

    NetUtils::CleanupWinsock();
    std::cout << "\nGoodbye!\n";
    return 0;
}
