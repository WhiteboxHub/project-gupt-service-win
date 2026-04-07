#include "input_sender.h"
#include "../common/utils.h"

// Static instance for hook callbacks
InputSender* InputSender::s_instance = nullptr;

InputSender::InputSender()
    : targetWindow(nullptr)
    , networkClient(nullptr)
    , capturing(false)
    , enabled(true)
    , windowHasFocus(false)
    , keyboardHook(nullptr)
    , mouseHook(nullptr)
    , mouseInitialized(false)
{
    lastMousePos = {0, 0};
}

InputSender::~InputSender() {
    Shutdown();
}

bool InputSender::Initialize(HWND window, NetworkClient* client, const InputSenderConfig& cfg) {
    if (capturing) {
        LOG_WARNING("InputSender already initialized");
        return true;
    }

    if (!window || !client) {
        LOG_ERROR("Invalid window handle or network client");
        return false;
    }

    LOG_INFO("Initializing input sender...");

    targetWindow = window;
    networkClient = client;
    config = cfg;
    s_instance = this;

    // Check window focus
    windowHasFocus = (GetForegroundWindow() == targetWindow);

    LOG_INFO("Input sender initialized");
    return true;
}

void InputSender::Shutdown() {
    if (!capturing && !keyboardHook && !mouseHook) {
        return;
    }

    LOG_INFO("Shutting down input sender...");

    Stop();

    s_instance = nullptr;
    targetWindow = nullptr;
    networkClient = nullptr;

    LOG_INFO("Input sender shutdown complete");
}

bool InputSender::Start() {
    if (capturing) {
        LOG_WARNING("Input sender already capturing");
        return true;
    }

    LOG_INFO("Starting input capture...");

    // Install keyboard hook
    if (config.captureKeyboard) {
        keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandle(nullptr), 0);
        if (!keyboardHook) {
            LOG_ERROR_FMT("Failed to install keyboard hook: %lu", GetLastError());
            return false;
        }
        LOG_INFO("Keyboard hook installed");
    }

    // Install mouse hook
    if (config.captureMouse) {
        mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(nullptr), 0);
        if (!mouseHook) {
            LOG_ERROR_FMT("Failed to install mouse hook: %lu", GetLastError());
            if (keyboardHook) {
                UnhookWindowsHookEx(keyboardHook);
                keyboardHook = nullptr;
            }
            return false;
        }
        LOG_INFO("Mouse hook installed");

        // Start mouse polling thread
        capturing = true;
        mouseThread = std::thread(&InputSender::MouseThread, this);
    } else {
        capturing = true;
    }

    LOG_INFO("Input capture started");
    return true;
}

void InputSender::Stop() {
    if (!capturing) {
        return;
    }

    LOG_INFO("Stopping input capture...");
    capturing = false;

    // Unhook keyboard
    if (keyboardHook) {
        UnhookWindowsHookEx(keyboardHook);
        keyboardHook = nullptr;
        LOG_INFO("Keyboard hook removed");
    }

    // Unhook mouse
    if (mouseHook) {
        UnhookWindowsHookEx(mouseHook);
        mouseHook = nullptr;
        LOG_INFO("Mouse hook removed");
    }

    // Stop mouse thread
    if (mouseThread.joinable()) {
        mouseThread.join();
    }

    LOG_INFO("Input capture stopped");
}

void InputSender::SetEnabled(bool enable) {
    enabled = enable;
    LOG_INFO_FMT("Input capture %s", enable ? "enabled" : "disabled");
}

void InputSender::MouseThread() {
    LOG_INFO("Mouse polling thread started");

    while (capturing) {
        if (enabled && windowHasFocus && networkClient && networkClient->IsConnected()) {
            // Get current cursor position
            POINT cursorPos;
            if (GetCursorPos(&cursorPos)) {
                if (!mouseInitialized) {
                    lastMousePos = cursorPos;
                    mouseInitialized = true;
                } else {
                    // Check if mouse moved
                    if (cursorPos.x != lastMousePos.x || cursorPos.y != lastMousePos.y) {
                        // Convert to client coordinates
                        POINT clientPos = cursorPos;
                        ScreenToClient(targetWindow, &clientPos);

                        SendMouseMove(clientPos.x, clientPos.y);
                        lastMousePos = cursorPos;
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(config.mouseSampleRateMs));
    }

    LOG_INFO("Mouse polling thread stopped");
}

void InputSender::SendKeyboardEvent(uint32_t vkCode, bool isDown) {
    if (!enabled || !networkClient || !networkClient->IsConnected()) {
        return;
    }

    // Only send if window has focus (or if we don't care about focus)
    if (config.captureRelativeMouseOnly && !windowHasFocus) {
        return;
    }

    InputPacket packet;
    packet.type = INPUT_TYPE_KEYBOARD;
    packet.keyboard.vkCode = vkCode;
    packet.keyboard.scanCode = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
    packet.keyboard.flags = isDown ? 0 : INPUT_FLAG_KEYUP;

    networkClient->SendInputEvent(packet);
    LOG_DEBUG_FMT("Sent keyboard event: VK=0x%02X %s", vkCode, isDown ? "DOWN" : "UP");
}

void InputSender::SendMouseButton(uint8_t button, bool isDown) {
    if (!enabled || !networkClient || !networkClient->IsConnected()) {
        return;
    }

    if (config.captureRelativeMouseOnly && !windowHasFocus) {
        return;
    }

    InputPacket packet;
    packet.type = INPUT_TYPE_MOUSE;
    packet.mouse.button = button;
    packet.mouse.flags = isDown ? 0 : INPUT_FLAG_MOUSEUP;

    // Get current cursor position
    POINT cursorPos;
    if (GetCursorPos(&cursorPos)) {
        ScreenToClient(targetWindow, &cursorPos);
        packet.mouse.x = cursorPos.x;
        packet.mouse.y = cursorPos.y;
    }

    networkClient->SendInputEvent(packet);
    LOG_DEBUG_FMT("Sent mouse button event: BTN=%u %s", button, isDown ? "DOWN" : "UP");
}

void InputSender::SendMouseMove(int32_t x, int32_t y) {
    if (!enabled || !networkClient || !networkClient->IsConnected()) {
        return;
    }

    if (config.captureRelativeMouseOnly && !windowHasFocus) {
        return;
    }

    InputPacket packet;
    packet.type = INPUT_TYPE_MOUSE;
    packet.mouse.x = x;
    packet.mouse.y = y;
    packet.mouse.flags = INPUT_FLAG_MOUSEMOVE;

    networkClient->SendInputEvent(packet);
    LOG_DEBUG_FMT("Sent mouse move event: (%d, %d)", x, y);
}

void InputSender::SendMouseWheel(int16_t delta) {
    if (!enabled || !networkClient || !networkClient->IsConnected()) {
        return;
    }

    if (config.captureRelativeMouseOnly && !windowHasFocus) {
        return;
    }

    InputPacket packet;
    packet.type = INPUT_TYPE_MOUSE;
    packet.mouse.wheelDelta = delta;
    packet.mouse.flags = INPUT_FLAG_MOUSEWHEEL;

    networkClient->SendInputEvent(packet);
    LOG_DEBUG_FMT("Sent mouse wheel event: delta=%d", delta);
}

LRESULT CALLBACK InputSender::KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        
        bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        s_instance->SendKeyboardEvent(kb->vkCode, isDown);
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK InputSender::MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance) {
        MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lParam;

        switch (wParam) {
            case WM_LBUTTONDOWN:
                s_instance->SendMouseButton(MOUSE_BUTTON_LEFT, true);
                break;
            case WM_LBUTTONUP:
                s_instance->SendMouseButton(MOUSE_BUTTON_LEFT, false);
                break;
            case WM_RBUTTONDOWN:
                s_instance->SendMouseButton(MOUSE_BUTTON_RIGHT, true);
                break;
            case WM_RBUTTONUP:
                s_instance->SendMouseButton(MOUSE_BUTTON_RIGHT, false);
                break;
            case WM_MBUTTONDOWN:
                s_instance->SendMouseButton(MOUSE_BUTTON_MIDDLE, true);
                break;
            case WM_MBUTTONUP:
                s_instance->SendMouseButton(MOUSE_BUTTON_MIDDLE, false);
                break;
            case WM_MOUSEWHEEL:
                s_instance->SendMouseWheel(GET_WHEEL_DELTA_WPARAM(ms->mouseData));
                break;
            case WM_MOUSEMOVE:
                // Mouse movement is handled by polling thread for better performance
                break;
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK InputSender::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (s_instance) {
        switch (msg) {
            case WM_SETFOCUS:
                s_instance->windowHasFocus = true;
                LOG_DEBUG("Window gained focus");
                break;
            case WM_KILLFOCUS:
                s_instance->windowHasFocus = false;
                LOG_DEBUG("Window lost focus");
                break;
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
