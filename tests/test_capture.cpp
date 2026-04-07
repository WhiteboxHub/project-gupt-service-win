#include "../host/capture.h"
#include "../common/utils.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <conio.h>

// ============================================================================
// Screen Capture Test Program
// ============================================================================

class CaptureTest {
public:
    void Run() {
        std::cout << "=== GuPT Screen Capture Test ===\n\n";

        // Initialize logger
        Logger::GetInstance().SetLogLevel(LogLevel::DEBUG);
        Logger::GetInstance().SetLogToConsole(true);

        // Show available monitors
        ListMonitors();

        // Run tests
        if (!TestBasicCapture()) {
            std::cerr << "\nBasic capture test FAILED!\n";
            return;
        }

        if (!TestPerformance()) {
            std::cerr << "\nPerformance test FAILED!\n";
            return;
        }

        if (!TestDirtyRegions()) {
            std::cerr << "\nDirty regions test FAILED!\n";
            return;
        }

        std::cout << "\n=== ALL TESTS PASSED ===\n";
    }

private:
    void ListMonitors() {
        std::cout << "\n--- Available Monitors ---\n";
        auto monitors = CaptureUtils::GetMonitorInfo();

        if (monitors.empty()) {
            std::cout << "No monitors found!\n";
            return;
        }

        for (size_t i = 0; i < monitors.size(); i++) {
            const auto& monitor = monitors[i];
            std::wcout << "Monitor " << i << ": " << monitor.deviceName << "\n";
            std::cout << "  Resolution: " << monitor.width << "x" << monitor.height << "\n";
            std::cout << "  Coordinates: (" << monitor.coordinates.left << ", " << monitor.coordinates.top
                      << ") - (" << monitor.coordinates.right << ", " << monitor.coordinates.bottom << ")\n";
            std::cout << "  Primary: " << (monitor.isPrimary ? "Yes" : "No") << "\n";
        }
        std::cout << "\n";
    }

    bool TestBasicCapture() {
        std::cout << "\n--- Test 1: Basic Capture ---\n";

        ScreenCapture capture;
        CaptureConfig config;
        config.adapterIndex = 0;
        config.outputIndex = 0;
        config.timeoutMs = 100;

        if (!capture.Initialize(config)) {
            std::cerr << "Failed to initialize capture\n";
            return false;
        }

        uint32_t width, height;
        capture.GetDimensions(width, height);
        std::cout << "Screen dimensions: " << width << "x" << height << "\n";

        // Capture a few frames
        std::cout << "Capturing 10 frames...\n";
        int successCount = 0;

        for (int i = 0; i < 10; i++) {
            ID3D11Texture2D* texture = nullptr;
            FrameInfo info;

            if (capture.CaptureFrame(&texture, info)) {
                if (info.hasFrameUpdate && texture) {
                    std::cout << "  Frame " << info.frameNumber
                              << ": " << info.width << "x" << info.height
                              << ", Dirty rects: " << info.dirtyRectCount
                              << "\n";

                    // Save first frame as BMP
                    if (i == 0) {
                        std::cout << "  Saving first frame to test_frame.bmp...\n";
                        if (CaptureUtils::SaveTextureToBMP(capture.GetDevice(), capture.GetContext(),
                                                           texture, L"test_frame.bmp")) {
                            std::cout << "  Frame saved successfully!\n";
                        }
                    }

                    successCount++;
                }

                capture.ReleaseFrame();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        capture.Shutdown();

        std::cout << "Captured " << successCount << " frames successfully\n";
        return successCount > 0;
    }

    bool TestPerformance() {
        std::cout << "\n--- Test 2: Performance Benchmark ---\n";

        ScreenCapture capture;
        if (!capture.Initialize()) {
            return false;
        }

        const int FRAME_COUNT = 100;
        std::cout << "Capturing " << FRAME_COUNT << " frames for performance measurement...\n";

        PerformanceTimer timer;
        uint64_t totalCaptureTime = 0;
        int framesWithUpdate = 0;

        for (int i = 0; i < FRAME_COUNT; i++) {
            ID3D11Texture2D* texture = nullptr;
            FrameInfo info;

            PerformanceTimer frameTimer;
            bool success = capture.CaptureFrame(&texture, info);
            uint64_t captureTime = frameTimer.ElapsedMicroseconds();

            if (success && info.hasFrameUpdate) {
                totalCaptureTime += captureTime;
                framesWithUpdate++;
                capture.ReleaseFrame();
            }

            // Don't sleep - measure max capture rate
        }

        double totalTime = timer.ElapsedMilliseconds();
        double avgCaptureTime = framesWithUpdate > 0 ? (double)totalCaptureTime / framesWithUpdate / 1000.0 : 0;
        double fps = (FRAME_COUNT * 1000.0) / totalTime;

        std::cout << "\nResults:\n";
        std::cout << "  Total time: " << totalTime << " ms\n";
        std::cout << "  Frames with updates: " << framesWithUpdate << "\n";
        std::cout << "  Average capture time: " << avgCaptureTime << " ms\n";
        std::cout << "  Average FPS: " << fps << "\n";

        // Check performance targets
        bool passedPerformance = avgCaptureTime < 5.0; // Target: <5ms
        std::cout << "\n  Performance target (<5ms): " << (passedPerformance ? "PASS" : "FAIL") << "\n";

        capture.Shutdown();
        return true; // Don't fail on performance, just report
    }

    bool TestDirtyRegions() {
        std::cout << "\n--- Test 3: Dirty Region Detection ---\n";
        std::cout << "This test requires you to move your mouse or type something...\n";
        std::cout << "Press any key to start (will run for 5 seconds)...\n";
        _getch();

        ScreenCapture capture;
        if (!capture.Initialize()) {
            return false;
        }

        std::cout << "Monitoring dirty regions for 5 seconds...\n";

        int frameCount = 0;
        int framesWithDirtyRects = 0;
        int totalDirtyRects = 0;

        auto startTime = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
            if (elapsed >= 5) {
                break;
            }

            ID3D11Texture2D* texture = nullptr;
            FrameInfo info;

            if (capture.CaptureFrame(&texture, info)) {
                if (info.hasFrameUpdate) {
                    frameCount++;
                    if (info.dirtyRectCount > 0) {
                        framesWithDirtyRects++;
                        totalDirtyRects += info.dirtyRectCount;

                        std::cout << "  Frame " << frameCount
                                  << " has " << info.dirtyRectCount << " dirty regions\n";

                        // Show first few dirty rects
                        for (size_t i = 0; i < std::min(info.dirtyRectCount, 3u); i++) {
                            const RECT& rect = info.dirtyRects[i];
                            std::cout << "    Rect " << i << ": ("
                                      << rect.left << ", " << rect.top << ") - ("
                                      << rect.right << ", " << rect.bottom << ")\n";
                        }
                    }
                    capture.ReleaseFrame();
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
        }

        std::cout << "\nDirty Region Summary:\n";
        std::cout << "  Total frames captured: " << frameCount << "\n";
        std::cout << "  Frames with dirty regions: " << framesWithDirtyRects << "\n";
        std::cout << "  Total dirty regions: " << totalDirtyRects << "\n";
        if (framesWithDirtyRects > 0) {
            std::cout << "  Average dirty regions per frame: "
                      << (double)totalDirtyRects / framesWithDirtyRects << "\n";
        }

        capture.Shutdown();
        return frameCount > 0;
    }
};

// ============================================================================
// Interactive Test Menu
// ============================================================================

void ShowMenu() {
    std::cout << "\n=== GuPT Screen Capture Test Menu ===\n";
    std::cout << "1. Run all tests\n";
    std::cout << "2. Basic capture test\n";
    std::cout << "3. Performance benchmark\n";
    std::cout << "4. Dirty regions test\n";
    std::cout << "5. Continuous capture (press 'q' to stop)\n";
    std::cout << "6. List monitors\n";
    std::cout << "0. Exit\n";
    std::cout << "\nChoice: ";
}

void ContinuousCapture() {
    std::cout << "\n--- Continuous Capture Mode ---\n";
    std::cout << "Press 'q' to stop...\n\n";

    Logger::GetInstance().SetLogLevel(LogLevel::INFO);

    ScreenCapture capture;
    if (!capture.Initialize()) {
        std::cerr << "Failed to initialize capture\n";
        return;
    }

    PerformanceMetrics metrics;
    FrameRateCalculator fpsCalc;

    std::atomic<bool> running(true);
    std::thread captureThread([&]() {
        while (running) {
            ID3D11Texture2D* texture = nullptr;
            FrameInfo info;

            ScopedTimer timer(metrics.captureTimeUs);
            bool success = capture.CaptureFrame(&texture, info);

            if (success && info.hasFrameUpdate) {
                metrics.framesCaptured++;
                fpsCalc.AddFrame();
                capture.ReleaseFrame();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
        }
    });

    // Monitor performance
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
            std::cout << "\rFPS: " << fps
                      << " | Frames: " << metrics.framesCaptured.load()
                      << " | Avg Capture: " << (metrics.captureTimeUs.load() / std::max(1u, metrics.framesCaptured.load())) / 1000.0 << " ms"
                      << "                    ";
            std::cout.flush();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    captureThread.join();
    capture.Shutdown();

    std::cout << "\n\nFinal Statistics:\n";
    metrics.Print();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    // Initialize Winsock (for compatibility with main project)
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

        CaptureTest test;

        switch (choice) {
            case 1:
                test.Run();
                break;
            case 2:
                test.TestBasicCapture();
                break;
            case 3:
                test.TestPerformance();
                break;
            case 4:
                test.TestDirtyRegions();
                break;
            case 5:
                ContinuousCapture();
                break;
            case 6: {
                auto monitors = CaptureUtils::GetMonitorInfo();
                std::cout << "\nFound " << monitors.size() << " monitors:\n";
                for (size_t i = 0; i < monitors.size(); i++) {
                    std::wcout << i << ". " << monitors[i].deviceName
                              << " - " << monitors[i].width << "x" << monitors[i].height << "\n";
                }
                break;
            }
            default:
                std::cout << "Invalid choice\n";
        }
    }

    NetUtils::CleanupWinsock();
    std::cout << "\nGoodbye!\n";
    return 0;
}
