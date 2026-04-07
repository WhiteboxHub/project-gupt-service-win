#pragma once

#include "../common/protocol.h"
#include "../common/packet.h"
#include "../common/logger.h"
#include "network_client.h"
#include <Windows.h>
#include <memory>
#include <atomic>
#include <thread>

// ============================================================================
// Input Sender - Captures local input and sends to remote host
// ============================================================================

struct InputSenderConfig {
    bool captureKeyboard = true;
    bool captureMouse = true;
    bool captureRelativeMouseOnly = false; // Only send when window has focus
    uint32_t mouseSampleRateMs = 16; // ~60Hz mouse polling
};

class InputSender {
public:
    InputSender();
    ~InputSender();

    InputSender(const InputSender&) = delete;
    InputSender& operator=(const InputSender&) = delete;

    // Initialize input capture
    bool Initialize(HWND targetWindow, NetworkClient* client, const InputSenderConfig& config);

    // Shutdown input capture
    void Shutdown();

    // Start capturing input (spawns thread)
    bool Start();

    // Stop capturing input
    void Stop();

    // Check if capturing
    bool IsCapturing() const { return capturing; }

    // Enable/disable input capture
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled; }

private:
    // Mouse polling thread
    void MouseThread();

    // Send keyboard event
    void SendKeyboardEvent(uint32_t vkCode, bool isDown);

    // Send mouse button event
    void SendMouseButton(uint8_t button, bool isDown);

    // Send mouse move event
    void SendMouseMove(int32_t x, int32_t y);

    // Send mouse wheel event
    void SendMouseWheel(int16_t delta);

    // Window procedure for message processing
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Low-level keyboard hook
    static LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);

    // Low-level mouse hook
    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);

    // Static instance for hook callbacks
    static InputSender* s_instance;

private:
    InputSenderConfig config;
    HWND targetWindow;
    NetworkClient* networkClient;

    std::atomic<bool> capturing;
    std::atomic<bool> enabled;
    std::atomic<bool> windowHasFocus;

    // Hooks
    HHOOK keyboardHook;
    HHOOK mouseHook;

    // Mouse polling thread
    std::thread mouseThread;

    // Last mouse position (for relative movement detection)
    POINT lastMousePos;
    bool mouseInitialized;
};
