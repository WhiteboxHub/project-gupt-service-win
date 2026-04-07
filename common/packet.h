#pragma once

#include "protocol.h"
#include <cstring>

// ============================================================================
// Packet Structures
// ============================================================================

// Ensure proper packing on Windows
#pragma pack(push, 1)

// ----------------------------------------------------------------------------
// Video Packet (UDP)
// ----------------------------------------------------------------------------

struct VideoPacketHeader {
    uint32_t magic;           // GUPT_MAGIC for validation
    uint16_t version;         // Protocol version
    uint16_t type;            // PacketType (FRAME/KEYFRAME)
    uint64_t frameId;         // Monotonic frame counter
    uint32_t sequenceNum;     // Packet sequence within frame
    uint32_t totalPackets;    // Total packets for this frame
    uint32_t dataSize;        // Actual payload size in bytes
    uint64_t timestamp;       // Capture timestamp (microseconds)
    uint8_t flags;            // PacketFlags
    uint8_t reserved[3];      // Padding for alignment

    VideoPacketHeader()
        : magic(GUPT_MAGIC)
        , version(PROTOCOL_VERSION)
        , type(PACKET_TYPE_FRAME)
        , frameId(0)
        , sequenceNum(0)
        , totalPackets(0)
        , dataSize(0)
        , timestamp(0)
        , flags(FLAG_NONE)
        , reserved{0, 0, 0}
    {}
};
static_assert(sizeof(VideoPacketHeader) == 40, "VideoPacketHeader size mismatch");

struct VideoPacket {
    VideoPacketHeader header;
    uint8_t data[MAX_UDP_PAYLOAD];

    VideoPacket() {
        std::memset(data, 0, sizeof(data));
    }

    bool IsValid() const {
        return header.magic == GUPT_MAGIC &&
               header.version == PROTOCOL_VERSION &&
               header.dataSize <= MAX_UDP_PAYLOAD;
    }

    size_t GetTotalSize() const {
        return sizeof(VideoPacketHeader) + header.dataSize;
    }
};

// ----------------------------------------------------------------------------
// Input Packet (TCP)
// ----------------------------------------------------------------------------

struct InputPacket {
    uint32_t magic;           // GUPT_MAGIC
    uint16_t version;         // Protocol version
    uint16_t type;            // InputType
    uint16_t flags;           // Button state, modifiers, etc.
    uint16_t reserved;
    int32_t x;                // Mouse X coordinate
    int32_t y;                // Mouse Y coordinate
    int32_t wheelDelta;       // Mouse wheel delta
    uint32_t vkCode;          // Virtual key code (keyboard)
    uint64_t timestamp;       // Client timestamp (microseconds)

    InputPacket()
        : magic(GUPT_MAGIC)
        , version(PROTOCOL_VERSION)
        , type(INPUT_MOUSE_MOVE)
        , flags(0)
        , reserved(0)
        , x(0)
        , y(0)
        , wheelDelta(0)
        , vkCode(0)
        , timestamp(0)
    {}

    bool IsValid() const {
        return magic == GUPT_MAGIC && version == PROTOCOL_VERSION;
    }
};
static_assert(sizeof(InputPacket) == 40, "InputPacket size mismatch");

// ----------------------------------------------------------------------------
// Handshake Packet (TCP)
// ----------------------------------------------------------------------------

struct HandshakePacket {
    uint32_t magic;           // GUPT_MAGIC
    uint16_t version;         // Protocol version
    uint16_t type;            // PACKET_TYPE_HANDSHAKE
    uint8_t clientVersion[3]; // Major.Minor.Patch
    uint8_t reserved1;
    uint32_t screenWidth;     // Client screen width
    uint32_t screenHeight;    // Client screen height
    uint32_t capabilities;    // Capability flags
    char sessionId[64];       // Session ID (if using signaling)
    uint8_t reserved2[32];

    HandshakePacket()
        : magic(GUPT_MAGIC)
        , version(PROTOCOL_VERSION)
        , type(PACKET_TYPE_HANDSHAKE)
        , clientVersion{GUPT_VERSION.major, GUPT_VERSION.minor, GUPT_VERSION.patch}
        , reserved1(0)
        , screenWidth(0)
        , screenHeight(0)
        , capabilities(0)
        , sessionId{0}
        , reserved2{0}
    {}

    bool IsValid() const {
        return magic == GUPT_MAGIC && version == PROTOCOL_VERSION;
    }
};
static_assert(sizeof(HandshakePacket) == 128, "HandshakePacket size mismatch");

// ----------------------------------------------------------------------------
// Authentication Packet (TCP)
// ----------------------------------------------------------------------------

struct AuthPacket {
    uint32_t magic;           // GUPT_MAGIC
    uint16_t version;         // Protocol version
    uint16_t type;            // PACKET_TYPE_AUTH
    uint8_t passwordHash[32]; // SHA-256 hash of password
    uint8_t reserved[24];

    AuthPacket()
        : magic(GUPT_MAGIC)
        , version(PROTOCOL_VERSION)
        , type(PACKET_TYPE_AUTH)
        , passwordHash{0}
        , reserved{0}
    {}

    bool IsValid() const {
        return magic == GUPT_MAGIC && version == PROTOCOL_VERSION;
    }
};
static_assert(sizeof(AuthPacket) == 64, "AuthPacket size mismatch");

// ----------------------------------------------------------------------------
// Control Packet (TCP)
// ----------------------------------------------------------------------------

struct ControlPacket {
    uint32_t magic;           // GUPT_MAGIC
    uint16_t version;         // Protocol version
    uint16_t type;            // Control packet type
    uint32_t payload;         // Type-specific payload
    uint64_t timestamp;       // Timestamp (microseconds)
    uint8_t data[48];         // Additional data

    ControlPacket()
        : magic(GUPT_MAGIC)
        , version(PROTOCOL_VERSION)
        , type(PACKET_TYPE_KEEPALIVE)
        , payload(0)
        , timestamp(0)
        , data{0}
    {}

    bool IsValid() const {
        return magic == GUPT_MAGIC && version == PROTOCOL_VERSION;
    }
};
static_assert(sizeof(ControlPacket) == 64, "ControlPacket size mismatch");

// ----------------------------------------------------------------------------
// Resolution Change Packet (TCP)
// ----------------------------------------------------------------------------

struct ResolutionPacket {
    uint32_t magic;           // GUPT_MAGIC
    uint16_t version;         // Protocol version
    uint16_t type;            // PACKET_TYPE_RESOLUTION
    uint32_t width;           // New width
    uint32_t height;          // New height
    uint32_t bitrate;         // Suggested bitrate
    uint8_t reserved[48];

    ResolutionPacket()
        : magic(GUPT_MAGIC)
        , version(PROTOCOL_VERSION)
        , type(PACKET_TYPE_RESOLUTION)
        , width(0)
        , height(0)
        , bitrate(0)
        , reserved{0}
    {}

    bool IsValid() const {
        return magic == GUPT_MAGIC && version == PROTOCOL_VERSION;
    }
};
static_assert(sizeof(ResolutionPacket) == 64, "ResolutionPacket size mismatch");

#pragma pack(pop)

// ============================================================================
// Packet Serialization Helpers
// ============================================================================

namespace PacketUtils {

    // Calculate packet count for frame
    inline uint32_t CalculatePacketCount(size_t bitstreamSize) {
        return static_cast<uint32_t>((bitstreamSize + MAX_UDP_PAYLOAD - 1) / MAX_UDP_PAYLOAD);
    }

    // Validate packet magic and version
    template<typename T>
    inline bool ValidatePacket(const T& packet) {
        return packet.magic == GUPT_MAGIC &&
               packet.version == PROTOCOL_VERSION;
    }

    // Copy data safely
    inline void SafeCopy(uint8_t* dest, const uint8_t* src, size_t destSize, size_t srcSize) {
        size_t copySize = (srcSize < destSize) ? srcSize : destSize;
        std::memcpy(dest, src, copySize);
        if (copySize < destSize) {
            std::memset(dest + copySize, 0, destSize - copySize);
        }
    }

    // Create video packet from bitstream chunk
    inline VideoPacket CreateVideoPacket(
        uint64_t frameId,
        uint32_t sequenceNum,
        uint32_t totalPackets,
        const uint8_t* data,
        size_t dataSize,
        uint64_t timestamp,
        bool isKeyframe,
        bool isLastPacket)
    {
        VideoPacket packet;
        packet.header.type = isKeyframe ? PACKET_TYPE_KEYFRAME : PACKET_TYPE_FRAME;
        packet.header.frameId = frameId;
        packet.header.sequenceNum = sequenceNum;
        packet.header.totalPackets = totalPackets;
        packet.header.dataSize = static_cast<uint32_t>(dataSize);
        packet.header.timestamp = timestamp;
        packet.header.flags = isLastPacket ? FLAG_LAST_PACKET : FLAG_NONE;
        if (isKeyframe) {
            packet.header.flags |= FLAG_IDR_FRAME;
        }

        SafeCopy(packet.data, data, MAX_UDP_PAYLOAD, dataSize);
        return packet;
    }

    // Create input packet
    inline InputPacket CreateMouseMovePacket(int32_t x, int32_t y, uint64_t timestamp) {
        InputPacket packet;
        packet.type = INPUT_MOUSE_MOVE;
        packet.x = x;
        packet.y = y;
        packet.timestamp = timestamp;
        return packet;
    }

    inline InputPacket CreateMouseButtonPacket(uint8_t button, bool down, uint64_t timestamp) {
        InputPacket packet;
        packet.type = INPUT_MOUSE_BUTTON;
        packet.flags = (button << 8) | (down ? 1 : 0);
        packet.timestamp = timestamp;
        return packet;
    }

    inline InputPacket CreateMouseWheelPacket(int32_t delta, uint64_t timestamp) {
        InputPacket packet;
        packet.type = INPUT_MOUSE_WHEEL;
        packet.wheelDelta = delta;
        packet.timestamp = timestamp;
        return packet;
    }

    inline InputPacket CreateKeyboardPacket(uint32_t vkCode, bool down, uint64_t timestamp) {
        InputPacket packet;
        packet.type = INPUT_KEYBOARD;
        packet.vkCode = vkCode;
        packet.flags = down ? 1 : 0;
        packet.timestamp = timestamp;
        return packet;
    }

} // namespace PacketUtils
