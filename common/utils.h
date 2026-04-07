#pragma once

#include "protocol.h"
#include <chrono>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

// ============================================================================
// High-Resolution Timer
// ============================================================================

class PerformanceTimer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    using Microseconds = std::chrono::microseconds;

    PerformanceTimer() : startTime(Clock::now()) {}

    void Reset() {
        startTime = Clock::now();
    }

    uint64_t ElapsedMicroseconds() const {
        auto now = Clock::now();
        auto elapsed = std::chrono::duration_cast<Microseconds>(now - startTime);
        return static_cast<uint64_t>(elapsed.count());
    }

    double ElapsedMilliseconds() const {
        return ElapsedMicroseconds() / 1000.0;
    }

    double ElapsedSeconds() const {
        return ElapsedMicroseconds() / 1000000.0;
    }

    static uint64_t GetTimestampMicroseconds() {
        auto now = Clock::now();
        auto epoch = now.time_since_epoch();
        auto micros = std::chrono::duration_cast<Microseconds>(epoch);
        return static_cast<uint64_t>(micros.count());
    }

private:
    TimePoint startTime;
};

// ============================================================================
// Thread-Safe Queue
// ============================================================================

template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() : stopped(false) {}

    ~ThreadSafeQueue() {
        Stop();
    }

    // Push item to queue
    void Push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(item);
        condition.notify_one();
    }

    void Push(T&& item) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(std::move(item));
        condition.notify_one();
    }

    // Pop item from queue (blocking with timeout)
    bool Pop(T& item, uint32_t timeoutMs = 0) {
        std::unique_lock<std::mutex> lock(mutex);

        if (timeoutMs == 0) {
            // Wait indefinitely
            condition.wait(lock, [this] { return !queue.empty() || stopped; });
        } else {
            // Wait with timeout
            auto timeout = std::chrono::milliseconds(timeoutMs);
            if (!condition.wait_for(lock, timeout, [this] { return !queue.empty() || stopped; })) {
                return false; // Timeout
            }
        }

        if (stopped && queue.empty()) {
            return false;
        }

        item = std::move(queue.front());
        queue.pop();
        return true;
    }

    // Try to pop without blocking
    bool TryPop(T& item) {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) {
            return false;
        }
        item = std::move(queue.front());
        queue.pop();
        return true;
    }

    // Get size
    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }

    // Clear queue
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex);
        std::queue<T> empty;
        std::swap(queue, empty);
    }

    // Stop queue (unblock all waiting threads)
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopped = true;
        }
        condition.notify_all();
    }

    void Resume() {
        std::lock_guard<std::mutex> lock(mutex);
        stopped = false;
    }

private:
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::queue<T> queue;
    bool stopped;
};

// ============================================================================
// Network Utilities
// ============================================================================

namespace NetUtils {

    // Initialize Winsock
    bool InitializeWinsock();

    // Cleanup Winsock
    void CleanupWinsock();

    // Get last socket error
    int GetLastSocketError();

    // Get error string
    std::string GetSocketErrorString(int errorCode);

    // Set socket blocking mode
    bool SetSocketBlocking(socket_t sock, bool blocking);

    // Set socket timeout
    bool SetSocketTimeout(socket_t sock, uint32_t recvTimeoutMs, uint32_t sendTimeoutMs);

    // Set socket reuse address
    bool SetSocketReuseAddr(socket_t sock, bool reuse);

    // Set TCP no delay (disable Nagle's algorithm)
    bool SetTcpNoDelay(socket_t sock, bool noDelay);

    // Set socket buffer sizes
    bool SetSocketBufferSize(socket_t sock, int recvBufferSize, int sendBufferSize);

    // Bind socket to port
    bool BindSocket(socket_t sock, uint16_t port, const char* ip = nullptr);

    // Get local address from socket
    bool GetLocalAddress(socket_t sock, std::string& outIp, uint16_t& outPort);

    // Get peer address from socket
    bool GetPeerAddress(socket_t sock, std::string& outIp, uint16_t& outPort);

    // Check if socket is valid
    inline bool IsValidSocket(socket_t sock) {
        return sock != INVALID_SOCKET_VALUE;
    }

    // Close socket safely
    void CloseSocket(socket_t& sock);

} // namespace NetUtils

// ============================================================================
// String Utilities
// ============================================================================

namespace StrUtils {

    // Convert to lowercase
    std::string ToLower(const std::string& str);

    // Convert to uppercase
    std::string ToUpper(const std::string& str);

    // Trim whitespace
    std::string Trim(const std::string& str);

    // Split string by delimiter
    std::vector<std::string> Split(const std::string& str, char delimiter);

    // Format string (like printf)
    std::string Format(const char* format, ...);

    // Parse IP:PORT string
    bool ParseAddress(const std::string& address, std::string& outIp, uint16_t& outPort);

} // namespace StrUtils

// ============================================================================
// Crypto Utilities (Simple hashing for authentication)
// ============================================================================

namespace CryptoUtils {

    // SHA-256 hash
    void SHA256(const uint8_t* data, size_t length, uint8_t* outHash);

    // SHA-256 hash from string
    void SHA256String(const std::string& str, uint8_t* outHash);

    // Convert hash to hex string
    std::string HashToHex(const uint8_t* hash, size_t length);

} // namespace CryptoUtils

// ============================================================================
// Performance Metrics
// ============================================================================

struct PerformanceMetrics {
    std::atomic<uint64_t> captureTimeUs{0};
    std::atomic<uint64_t> encodeTimeUs{0};
    std::atomic<uint64_t> networkTimeUs{0};
    std::atomic<uint64_t> decodeTimeUs{0};
    std::atomic<uint64_t> renderTimeUs{0};
    std::atomic<uint64_t> totalLatencyUs{0};

    std::atomic<uint32_t> framesCaptured{0};
    std::atomic<uint32_t> framesEncoded{0};
    std::atomic<uint32_t> framesSent{0};
    std::atomic<uint32_t> framesReceived{0};
    std::atomic<uint32_t> framesDecoded{0};
    std::atomic<uint32_t> framesRendered{0};

    std::atomic<uint64_t> bytesSent{0};
    std::atomic<uint64_t> bytesReceived{0};
    std::atomic<uint32_t> packetsLost{0};

    std::atomic<uint32_t> currentFps{0};
    std::atomic<uint32_t> currentBitrate{0};

    void Reset() {
        captureTimeUs = 0;
        encodeTimeUs = 0;
        networkTimeUs = 0;
        decodeTimeUs = 0;
        renderTimeUs = 0;
        totalLatencyUs = 0;
        framesCaptured = 0;
        framesEncoded = 0;
        framesSent = 0;
        framesReceived = 0;
        framesDecoded = 0;
        framesRendered = 0;
        bytesSent = 0;
        bytesReceived = 0;
        packetsLost = 0;
        currentFps = 0;
        currentBitrate = 0;
    }

    void Print() const;
};

// ============================================================================
// Scoped Timer (RAII for measuring execution time)
// ============================================================================

class ScopedTimer {
public:
    ScopedTimer(std::atomic<uint64_t>& accumulator)
        : accumulator(accumulator)
        , timer()
    {}

    ~ScopedTimer() {
        accumulator += timer.ElapsedMicroseconds();
    }

private:
    std::atomic<uint64_t>& accumulator;
    PerformanceTimer timer;
};

// ============================================================================
// Frame Rate Calculator
// ============================================================================

class FrameRateCalculator {
public:
    FrameRateCalculator() : frameCount(0), lastTime(PerformanceTimer::GetTimestampMicroseconds()) {}

    void AddFrame() {
        frameCount++;
    }

    uint32_t CalculateFPS() {
        uint64_t currentTime = PerformanceTimer::GetTimestampMicroseconds();
        uint64_t elapsedUs = currentTime - lastTime;

        if (elapsedUs >= 1000000) { // 1 second
            uint32_t fps = static_cast<uint32_t>((frameCount * 1000000ULL) / elapsedUs);
            frameCount = 0;
            lastTime = currentTime;
            return fps;
        }

        return 0; // Not ready yet
    }

private:
    uint32_t frameCount;
    uint64_t lastTime;
};
