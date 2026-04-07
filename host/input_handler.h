#pragma once

#include "../common/protocol.h"
#include "../common/packet.h"
#include "../common/logger.h"
#include <Windows.h>
#include <atomic>
#include <mutex>

// ============================================================================
// Input Handler for Remote Control
// ============================================================================

struct InputHandlerConfig {
    uint32_t screenWidth = 1920;
    uint32_t screenHeight = 1080;
    bool enableKeyboard = true;
    bool enableMouse = true;
    bool clipCursor = false;            // Clip cursor to screen bounds
    bool relativeMouseMovement = false;  // Use relative vs absolute coordinates
};

struct InputStats {
    std::atomic<uint64_t> keyboardEvents{0};
    std::atomic<uint64_t> mouseEvents{0};
    std::atomic<uint64_t> mouseMoveEvents{0};
    std::atomic<uint64_t> mouseButtonEvents{0};
    std::atomic<uint64_t> mouseWheelEvents{0};
    std::atomic<uint64_t> invalidPackets{0};

    void Reset() {
        keyboardEvents = 0;
        mouseEvents = 0;
        mouseMoveEvents = 0;
        mouseButtonEvents = 0;
        mouseWheelEvents = 0;
        invalidPackets = 0;
    }

    void Print() const;
};

class InputHandler {
public:
    InputHandler();
    ~InputHandler();

    // Delete copy/move
    InputHandler(const InputHandler&) = delete;
    InputHandler& operator=(const InputHandler&) = delete;

    // Initialize input handler
    bool Initialize(const InputHandlerConfig& config);

    // Shutdown input handler
    void Shutdown();

    // Handle incoming input packet
    bool HandleInputPacket(const InputPacket& packet);

    // Enable/disable input injection
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled; }

    // Update screen dimensions (for coordinate mapping)
    void UpdateScreenDimensions(uint32_t width, uint32_t height);

    // Get statistics
    const InputStats& GetStats() const { return stats; }

    // Check if initialized
    bool IsInitialized() const { return initialized; }

private:
    // Inject keyboard event
    bool InjectKeyboard(uint32_t vkCode, bool keyDown);

    // Inject mouse movement
    bool InjectMouseMove(int32_t x, int32_t y);

    // Inject mouse button click
    bool InjectMouseButton(uint8_t button, bool down);

    // Inject mouse wheel scroll
    bool InjectMouseWheel(int32_t delta);

    // Map client coordinates to host coordinates
    void MapCoordinates(int32_t& x, int32_t& y) const;

    // Validate virtual key code
    bool IsValidVirtualKey(uint32_t vkCode) const;

private:
    InputHandlerConfig config;
    bool initialized;
    std::atomic<bool> enabled;

    // Screen dimensions
    std::atomic<uint32_t> screenWidth;
    std::atomic<uint32_t> screenHeight;

    // Statistics
    InputStats stats;

    // Mutex for thread safety
    mutable std::mutex mutex;
};

// ============================================================================
// Input Utilities
// ============================================================================

namespace InputUtils {

    // Convert button index to Windows mouse flag
    DWORD GetMouseButtonDownFlag(uint8_t button);
    DWORD GetMouseButtonUpFlag(uint8_t button);

    // Get current cursor position
    bool GetCursorPosition(int32_t& x, int32_t& y);

    // Set cursor position
    bool SetCursorPosition(int32_t x, int32_t y);

    // Check if key is currently pressed
    bool IsKeyPressed(uint32_t vkCode);

} // namespace InputUtils
