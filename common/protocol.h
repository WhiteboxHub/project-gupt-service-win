#pragma once

#include <cstdint>

// ============================================================================
// GUPT Protocol Definitions
// ============================================================================

// Protocol constants
#define GUPT_MAGIC          0x47555054  // "GUPT" in hex
#define PROTOCOL_VERSION    1

// Default ports
#define DEFAULT_TCP_PORT    5900
#define DEFAULT_UDP_PORT    5901

// Network parameters
#define MAX_UDP_PACKET_SIZE 1400  // MTU 1500 - IP(20) - UDP(8) - margin
#define MAX_UDP_PAYLOAD     (MAX_UDP_PACKET_SIZE - sizeof(VideoPacketHeader))

// Packet types
enum PacketType : uint16_t {
    // Video packets (UDP)
    PACKET_TYPE_FRAME       = 0x0100,
    PACKET_TYPE_KEYFRAME    = 0x0101,

    // Control packets (TCP)
    PACKET_TYPE_HANDSHAKE   = 0x0200,
    PACKET_TYPE_AUTH        = 0x0201,
    PACKET_TYPE_KEEPALIVE   = 0x0202,
    PACKET_TYPE_DISCONNECT  = 0x0203,
    PACKET_TYPE_RESOLUTION  = 0x0204,
    PACKET_TYPE_BITRATE     = 0x0205,

    // Input packets (TCP)
    PACKET_TYPE_INPUT       = 0x0300,
};

// Input types
enum InputType : uint16_t {
    INPUT_MOUSE_MOVE        = 0x0001,
    INPUT_MOUSE_BUTTON      = 0x0002,
    INPUT_MOUSE_WHEEL       = 0x0003,
    INPUT_KEYBOARD          = 0x0004,
};

// Packet flags
enum PacketFlags : uint8_t {
    FLAG_NONE               = 0x00,
    FLAG_LAST_PACKET        = 0x01,
    FLAG_REQUEST_KEYFRAME   = 0x02,
    FLAG_IDR_FRAME          = 0x04,
};

// Error codes
enum ErrorCode : int32_t {
    ERR_SUCCESS             = 0,
    ERR_SOCKET_FAILED       = -1,
    ERR_BIND_FAILED         = -2,
    ERR_CONNECT_FAILED      = -3,
    ERR_SEND_FAILED         = -4,
    ERR_RECV_FAILED         = -5,
    ERR_INVALID_PACKET      = -6,
    ERR_AUTH_FAILED         = -7,
    ERR_TIMEOUT             = -8,
    ERR_NOT_INITIALIZED     = -9,
    ERR_ALREADY_CONNECTED   = -10,
    ERR_NOT_CONNECTED       = -11,
};

// Connection states
enum ConnectionState : uint8_t {
    STATE_DISCONNECTED      = 0,
    STATE_CONNECTING        = 1,
    STATE_AUTHENTICATING    = 2,
    STATE_CONNECTED         = 3,
    STATE_ERROR             = 4,
};

// Video codec types
enum VideoCodec : uint8_t {
    CODEC_H264              = 0,
    CODEC_HEVC              = 1,
    CODEC_AV1               = 2,
};

// Configuration constants
namespace Config {
    // Video
    constexpr uint32_t DEFAULT_WIDTH        = 1920;
    constexpr uint32_t DEFAULT_HEIGHT       = 1080;
    constexpr uint32_t DEFAULT_FRAMERATE    = 30;
    constexpr uint32_t DEFAULT_BITRATE      = 5000000;  // 5 Mbps
    constexpr uint32_t MIN_BITRATE          = 1000000;  // 1 Mbps
    constexpr uint32_t MAX_BITRATE          = 20000000; // 20 Mbps

    // Network
    constexpr uint32_t TCP_RECV_TIMEOUT_MS  = 5000;
    constexpr uint32_t UDP_RECV_TIMEOUT_MS  = 1000;
    constexpr uint32_t KEEPALIVE_INTERVAL_MS = 2000;
    constexpr uint32_t CONNECTION_TIMEOUT_MS = 10000;

    // Queue sizes
    constexpr size_t CAPTURE_QUEUE_SIZE     = 3;
    constexpr size_t ENCODER_QUEUE_SIZE     = 5;
    constexpr size_t DECODER_QUEUE_SIZE     = 5;
    constexpr size_t RENDER_QUEUE_SIZE      = 2;

    // Performance
    constexpr uint32_t TARGET_LATENCY_MS    = 100;
    constexpr uint32_t CAPTURE_INTERVAL_MS  = 16;  // ~60 FPS
    constexpr uint32_t JITTER_BUFFER_MS     = 50;

    // Timeouts
    constexpr uint64_t FRAME_TIMEOUT_US     = 100000;  // 100ms
    constexpr uint64_t PACKET_TIMEOUT_US    = 50000;   // 50ms
}

// Helper macros
#define GUPT_UNUSED(x) (void)(x)
#define GUPT_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// Version information
struct Version {
    uint8_t major = 1;
    uint8_t minor = 0;
    uint8_t patch = 0;

    constexpr Version() = default;
    constexpr Version(uint8_t maj, uint8_t min, uint8_t pat)
        : major(maj), minor(min), patch(pat) {}

    constexpr bool operator==(const Version& other) const {
        return major == other.major && minor == other.minor && patch == other.patch;
    }

    constexpr bool operator>=(const Version& other) const {
        if (major > other.major) return true;
        if (major < other.major) return false;
        if (minor > other.minor) return true;
        if (minor < other.minor) return false;
        return patch >= other.patch;
    }
};

constexpr Version GUPT_VERSION(1, 0, 0);

// Platform-specific includes
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    // Socket compatibility
    using socket_t = SOCKET;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
    #define closesocket_compat(s) closesocket(s)
#else
    #error "Only Windows is supported"
#endif
