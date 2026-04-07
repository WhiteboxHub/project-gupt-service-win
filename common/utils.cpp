#include "utils.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdarg>
#include <wincrypt.h>

#pragma comment(lib, "advapi32.lib")

// ============================================================================
// Network Utilities Implementation
// ============================================================================

namespace NetUtils {

bool InitializeWinsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << "\n";
        return false;
    }
    return true;
}

void CleanupWinsock() {
    WSACleanup();
}

int GetLastSocketError() {
    return WSAGetLastError();
}

std::string GetSocketErrorString(int errorCode) {
    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&messageBuffer,
        0,
        nullptr
    );

    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);
    return message;
}

bool SetSocketBlocking(socket_t sock, bool blocking) {
    u_long mode = blocking ? 0 : 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
}

bool SetSocketTimeout(socket_t sock, uint32_t recvTimeoutMs, uint32_t sendTimeoutMs) {
    DWORD recvTimeout = recvTimeoutMs;
    DWORD sendTimeout = sendTimeoutMs;

    bool success = true;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&recvTimeout, sizeof(recvTimeout)) == SOCKET_ERROR) {
        success = false;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sendTimeout, sizeof(sendTimeout)) == SOCKET_ERROR) {
        success = false;
    }
    return success;
}

bool SetSocketReuseAddr(socket_t sock, bool reuse) {
    int value = reuse ? 1 : 0;
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&value, sizeof(value)) == 0;
}

bool SetTcpNoDelay(socket_t sock, bool noDelay) {
    int value = noDelay ? 1 : 0;
    return setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&value, sizeof(value)) == 0;
}

bool SetSocketBufferSize(socket_t sock, int recvBufferSize, int sendBufferSize) {
    bool success = true;
    if (recvBufferSize > 0) {
        if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char*)&recvBufferSize, sizeof(recvBufferSize)) == SOCKET_ERROR) {
            success = false;
        }
    }
    if (sendBufferSize > 0) {
        if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char*)&sendBufferSize, sizeof(sendBufferSize)) == SOCKET_ERROR) {
            success = false;
        }
    }
    return success;
}

bool BindSocket(socket_t sock, uint16_t port, const char* ip) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (ip == nullptr) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, ip, &addr.sin_addr);
    }

    return bind(sock, (sockaddr*)&addr, sizeof(addr)) == 0;
}

bool GetLocalAddress(socket_t sock, std::string& outIp, uint16_t& outPort) {
    sockaddr_in addr{};
    int addrLen = sizeof(addr);
    if (getsockname(sock, (sockaddr*)&addr, &addrLen) == 0) {
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ipStr, sizeof(ipStr));
        outIp = ipStr;
        outPort = ntohs(addr.sin_port);
        return true;
    }
    return false;
}

bool GetPeerAddress(socket_t sock, std::string& outIp, uint16_t& outPort) {
    sockaddr_in addr{};
    int addrLen = sizeof(addr);
    if (getpeername(sock, (sockaddr*)&addr, &addrLen) == 0) {
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ipStr, sizeof(ipStr));
        outIp = ipStr;
        outPort = ntohs(addr.sin_port);
        return true;
    }
    return false;
}

void CloseSocket(socket_t& sock) {
    if (IsValidSocket(sock)) {
        closesocket_compat(sock);
        sock = INVALID_SOCKET_VALUE;
    }
}

} // namespace NetUtils

// ============================================================================
// String Utilities Implementation
// ============================================================================

namespace StrUtils {

std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string ToUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

std::string Trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    size_t end = str.find_last_not_of(" \t\r\n");

    if (start == std::string::npos) {
        return "";
    }
    return str.substr(start, end - start + 1);
}

std::vector<std::string> Split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string Format(const char* format, ...) {
    va_list args;
    va_start(args, format);

    va_list argsCopy;
    va_copy(argsCopy, args);
    int size = vsnprintf(nullptr, 0, format, argsCopy);
    va_end(argsCopy);

    if (size < 0) {
        va_end(args);
        return "";
    }

    std::string result(size + 1, '\0');
    vsnprintf(&result[0], size + 1, format, args);
    va_end(args);

    result.resize(size);
    return result;
}

bool ParseAddress(const std::string& address, std::string& outIp, uint16_t& outPort) {
    size_t colonPos = address.find(':');
    if (colonPos == std::string::npos) {
        return false;
    }

    outIp = address.substr(0, colonPos);
    try {
        outPort = static_cast<uint16_t>(std::stoi(address.substr(colonPos + 1)));
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace StrUtils

// ============================================================================
// Crypto Utilities Implementation
// ============================================================================

namespace CryptoUtils {

void SHA256(const uint8_t* data, size_t length, uint8_t* outHash) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return;
    }

    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return;
    }

    if (!CryptHashData(hHash, data, static_cast<DWORD>(length), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return;
    }

    DWORD hashLen = 32;
    CryptGetHashParam(hHash, HP_HASHVAL, outHash, &hashLen, 0);

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
}

void SHA256String(const std::string& str, uint8_t* outHash) {
    SHA256(reinterpret_cast<const uint8_t*>(str.data()), str.size(), outHash);
}

std::string HashToHex(const uint8_t* hash, size_t length) {
    std::ostringstream oss;
    for (size_t i = 0; i < length; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

} // namespace CryptoUtils

// ============================================================================
// Performance Metrics Implementation
// ============================================================================

void PerformanceMetrics::Print() const {
    std::cout << "\n=== Performance Metrics ===\n";
    std::cout << "Capture Time:  " << captureTimeUs.load() << " us\n";
    std::cout << "Encode Time:   " << encodeTimeUs.load() << " us\n";
    std::cout << "Network Time:  " << networkTimeUs.load() << " us\n";
    std::cout << "Decode Time:   " << decodeTimeUs.load() << " us\n";
    std::cout << "Render Time:   " << renderTimeUs.load() << " us\n";
    std::cout << "Total Latency: " << totalLatencyUs.load() << " us\n\n";

    std::cout << "Frames Captured:  " << framesCaptured.load() << "\n";
    std::cout << "Frames Encoded:   " << framesEncoded.load() << "\n";
    std::cout << "Frames Sent:      " << framesSent.load() << "\n";
    std::cout << "Frames Received:  " << framesReceived.load() << "\n";
    std::cout << "Frames Decoded:   " << framesDecoded.load() << "\n";
    std::cout << "Frames Rendered:  " << framesRendered.load() << "\n\n";

    std::cout << "Bytes Sent:       " << bytesSent.load() << "\n";
    std::cout << "Bytes Received:   " << bytesReceived.load() << "\n";
    std::cout << "Packets Lost:     " << packetsLost.load() << "\n\n";

    std::cout << "Current FPS:      " << currentFps.load() << "\n";
    std::cout << "Current Bitrate:  " << currentBitrate.load() << " bps\n";
    std::cout << "==========================\n\n";
}
