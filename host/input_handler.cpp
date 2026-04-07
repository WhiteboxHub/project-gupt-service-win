#include "input_handler.h"
#include "../common/utils.h"
#include <iostream>

// ============================================================================
// InputStats Implementation
// ============================================================================

void InputStats::Print() const {
    std::cout << "=== Input Statistics ===\n";
    std::cout << "  Keyboard events:    " << keyboardEvents << "\n";
    std::cout << "  Mouse move events:  " << mouseMoveEvents << "\n";
    std::cout << "  Mouse button events:" << mouseButtonEvents << "\n";
    std::cout << "  Mouse wheel events: " << mouseWheelEvents << "\n";
    std::cout << "  Total mouse events: " << mouseEvents << "\n";
    std::cout << "  Invalid packets:    " << invalidPackets << "\n";
}

// ============================================================================
// InputHandler Implementation
// ============================================================================

InputHandler::InputHandler()
    : initialized(false)
    , enabled(true)
    , screenWidth(1920)
    , screenHeight(1080)
{
}

InputHandler::~InputHandler() {
    Shutdown();
}

bool InputHandler::Initialize(const InputHandlerConfig& cfg) {
    if (initialized) {
        LOG_WARNING("InputHandler already initialized");
        return true;
    }

    config = cfg;
    screenWidth = config.screenWidth;
    screenHeight = config.screenHeight;

    LOG_INFO_FMT("InputHandler initialized: %ux%u, Keyboard: %s, Mouse: %s",
                 config.screenWidth, config.screenHeight,
                 config.enableKeyboard ? "ON" : "OFF",
                 config.enableMouse ? "ON" : "OFF");

    initialized = true;
    enabled = true;
    stats.Reset();

    return true;
}

void InputHandler::Shutdown() {
    if (!initialized) {
        return;
    }

    LOG_INFO("Shutting down InputHandler...");
    enabled = false;
    initialized = false;
}

void InputHandler::SetEnabled(bool enable) {
    enabled = enable;
    LOG_INFO_FMT("Input injection %s", enable ? "enabled" : "disabled");
}

void InputHandler::UpdateScreenDimensions(uint32_t width, uint32_t height) {
    screenWidth = width;
    screenHeight = height;
    LOG_INFO_FMT("Screen dimensions updated: %ux%u", width, height);
}

bool InputHandler::HandleInputPacket(const InputPacket& packet) {
    if (!initialized || !enabled) {
        return false;
    }

    // Validate packet
    if (!packet.IsValid()) {
        stats.invalidPackets++;
        LOG_WARNING("Invalid input packet received");
        return false;
    }

    // Handle based on input type
    switch (packet.type) {
        case INPUT_MOUSE_MOVE:
            return InjectMouseMove(packet.x, packet.y);

        case INPUT_MOUSE_BUTTON:
            return InjectMouseButton((packet.flags >> 8) & 0xFF, (packet.flags & 0x01) != 0);

        case INPUT_MOUSE_WHEEL:
            return InjectMouseWheel(packet.wheelDelta);

        case INPUT_KEYBOARD:
            return InjectKeyboard(packet.vkCode, (packet.flags & 0x01) != 0);

        default:
            LOG_WARNING_FMT("Unknown input type: 0x%04X", packet.type);
            stats.invalidPackets++;
            return false;
    }
}

// ============================================================================
// Input Injection Implementation
// ============================================================================

bool InputHandler::InjectKeyboard(uint32_t vkCode, bool keyDown) {
    if (!config.enableKeyboard) {
        return false;
    }

    // Validate virtual key
    if (!IsValidVirtualKey(vkCode)) {
        LOG_WARNING_FMT("Invalid virtual key code: 0x%X", vkCode);
        stats.invalidPackets++;
        return false;
    }

    // Create INPUT structure
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(vkCode);
    input.ki.wScan = static_cast<WORD>(MapVirtualKey(vkCode, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = keyDown ? 0 : KEYEVENTF_KEYUP;
    input.ki.time = 0;
    input.ki.dwExtraInfo = 0;

    // Handle extended keys (arrows, home, end, etc.)
    if (vkCode >= VK_PRIOR && vkCode <= VK_DOWN) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }

    // Inject input
    UINT result = SendInput(1, &input, sizeof(INPUT));
    if (result != 1) {
        DWORD error = GetLastError();
        LOG_ERROR_FMT("SendInput failed for keyboard: error=%lu", error);
        return false;
    }

    stats.keyboardEvents++;
    return true;
}

bool InputHandler::InjectMouseMove(int32_t x, int32_t y) {
    if (!config.enableMouse) {
        return false;
    }

    // Map coordinates
    MapCoordinates(x, y);

    // Clamp to screen bounds
    int32_t screenW = static_cast<int32_t>(screenWidth.load());
    int32_t screenH = static_cast<int32_t>(screenHeight.load());
    x = (x < 0) ? 0 : ((x >= screenW) ? screenW - 1 : x);
    y = (y < 0) ? 0 : ((y >= screenH) ? screenH - 1 : y);

    // Convert to absolute coordinates (0-65535 range)
    DWORD absX = static_cast<DWORD>((x * 65535) / screenW);
    DWORD absY = static_cast<DWORD>((y * 65535) / screenH);

    // Create INPUT structure
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dx = absX;
    input.mi.dy = absY;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    input.mi.time = 0;
    input.mi.dwExtraInfo = 0;

    // Inject input
    UINT result = SendInput(1, &input, sizeof(INPUT));
    if (result != 1) {
        DWORD error = GetLastError();
        LOG_ERROR_FMT("SendInput failed for mouse move: error=%lu", error);
        return false;
    }

    stats.mouseMoveEvents++;
    stats.mouseEvents++;
    return true;
}

bool InputHandler::InjectMouseButton(uint8_t button, bool down) {
    if (!config.enableMouse) {
        return false;
    }

    // Get mouse button flags
    DWORD flags = down ? InputUtils::GetMouseButtonDownFlag(button)
                       : InputUtils::GetMouseButtonUpFlag(button);

    if (flags == 0) {
        LOG_WARNING_FMT("Invalid mouse button: %u", button);
        stats.invalidPackets++;
        return false;
    }

    // Create INPUT structure
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    input.mi.time = 0;
    input.mi.dwExtraInfo = 0;

    // Inject input
    UINT result = SendInput(1, &input, sizeof(INPUT));
    if (result != 1) {
        DWORD error = GetLastError();
        LOG_ERROR_FMT("SendInput failed for mouse button: error=%lu", error);
        return false;
    }

    stats.mouseButtonEvents++;
    stats.mouseEvents++;
    return true;
}

bool InputHandler::InjectMouseWheel(int32_t delta) {
    if (!config.enableMouse) {
        return false;
    }

    // Create INPUT structure
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(delta);
    input.mi.time = 0;
    input.mi.dwExtraInfo = 0;

    // Inject input
    UINT result = SendInput(1, &input, sizeof(INPUT));
    if (result != 1) {
        DWORD error = GetLastError();
        LOG_ERROR_FMT("SendInput failed for mouse wheel: error=%lu", error);
        return false;
    }

    stats.mouseWheelEvents++;
    stats.mouseEvents++;
    return true;
}

// ============================================================================
// Helper Functions
// ============================================================================

void InputHandler::MapCoordinates(int32_t& x, int32_t& y) const {
    // Currently using direct mapping (client and host have same resolution)
    // In future, can implement scaling for different resolutions
    // Example: x = (x * hostWidth) / clientWidth;
}

bool InputHandler::IsValidVirtualKey(uint32_t vkCode) const {
    // Valid range for Windows virtual key codes
    return (vkCode >= 0x01 && vkCode <= 0xFE);
}

// ============================================================================
// InputUtils Implementation
// ============================================================================

namespace InputUtils {

DWORD GetMouseButtonDownFlag(uint8_t button) {
    switch (button) {
        case 0: return MOUSEEVENTF_LEFTDOWN;   // Left button
        case 1: return MOUSEEVENTF_RIGHTDOWN;  // Right button
        case 2: return MOUSEEVENTF_MIDDLEDOWN; // Middle button
        case 3: return MOUSEEVENTF_XDOWN;      // X1 button
        case 4: return MOUSEEVENTF_XDOWN;      // X2 button
        default: return 0;
    }
}

DWORD GetMouseButtonUpFlag(uint8_t button) {
    switch (button) {
        case 0: return MOUSEEVENTF_LEFTUP;     // Left button
        case 1: return MOUSEEVENTF_RIGHTUP;    // Right button
        case 2: return MOUSEEVENTF_MIDDLEUP;   // Middle button
        case 3: return MOUSEEVENTF_XUP;        // X1 button
        case 4: return MOUSEEVENTF_XUP;        // X2 button
        default: return 0;
    }
}

bool GetCursorPosition(int32_t& x, int32_t& y) {
    POINT pt;
    if (GetCursorPos(&pt)) {
        x = pt.x;
        y = pt.y;
        return true;
    }
    return false;
}

bool SetCursorPosition(int32_t x, int32_t y) {
    return SetCursorPos(x, y) != FALSE;
}

bool IsKeyPressed(uint32_t vkCode) {
    return (GetAsyncKeyState(vkCode) & 0x8000) != 0;
}

} // namespace InputUtils
