#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <iostream>
#include <sstream>
#include <ctime>

// ============================================================================
// Logger - Thread-safe logging system
// ============================================================================

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARNING = 3,
    ERROR = 4,
    CRITICAL = 5
};

class Logger {
public:
    static Logger& GetInstance() {
        static Logger instance;
        return instance;
    }

    void SetLogLevel(LogLevel level) {
        minLevel = level;
    }

    void SetLogFile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex);
        if (fileStream.is_open()) {
            fileStream.close();
        }
        fileStream.open(filename, std::ios::app);
        logToFile = fileStream.is_open();
    }

    void SetLogToConsole(bool enabled) {
        logToConsole = enabled;
    }

    void Log(LogLevel level, const char* file, int line, const std::string& message) {
        if (level < minLevel) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex);

        std::string timestamp = GetTimestamp();
        std::string levelStr = LogLevelToString(level);
        std::string fullMessage = timestamp + " [" + levelStr + "] " + message;

        if (logToConsole) {
            if (level >= LogLevel::ERROR) {
                std::cerr << fullMessage << " (" << file << ":" << line << ")\n";
            } else {
                std::cout << fullMessage << "\n";
            }
        }

        if (logToFile && fileStream.is_open()) {
            fileStream << fullMessage << " (" << file << ":" << line << ")\n";
            fileStream.flush();
        }
    }

    template<typename... Args>
    void LogFormat(LogLevel level, const char* file, int line, const char* format, Args... args) {
        if (level < minLevel) {
            return;
        }

        char buffer[1024];
        snprintf(buffer, sizeof(buffer), format, args...);
        Log(level, file, line, std::string(buffer));
    }

private:
    Logger()
        : minLevel(LogLevel::INFO)
        , logToConsole(true)
        , logToFile(false)
    {}

    ~Logger() {
        if (fileStream.is_open()) {
            fileStream.close();
        }
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string GetTimestamp() {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
        return std::string(buffer);
    }

    std::string LogLevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE:    return "TRACE";
            case LogLevel::DEBUG:    return "DEBUG";
            case LogLevel::INFO:     return "INFO";
            case LogLevel::WARNING:  return "WARN";
            case LogLevel::ERROR:    return "ERROR";
            case LogLevel::CRITICAL: return "CRIT";
            default:                 return "UNKNOWN";
        }
    }

    LogLevel minLevel;
    bool logToConsole;
    bool logToFile;
    std::ofstream fileStream;
    std::mutex mutex;
};

// Logging macros
#define LOG_TRACE(msg)    Logger::GetInstance().Log(LogLevel::TRACE, __FILE__, __LINE__, msg)
#define LOG_DEBUG(msg)    Logger::GetInstance().Log(LogLevel::DEBUG, __FILE__, __LINE__, msg)
#define LOG_INFO(msg)     Logger::GetInstance().Log(LogLevel::INFO, __FILE__, __LINE__, msg)
#define LOG_WARNING(msg)  Logger::GetInstance().Log(LogLevel::WARNING, __FILE__, __LINE__, msg)
#define LOG_ERROR(msg)    Logger::GetInstance().Log(LogLevel::ERROR, __FILE__, __LINE__, msg)
#define LOG_CRITICAL(msg) Logger::GetInstance().Log(LogLevel::CRITICAL, __FILE__, __LINE__, msg)

// Format logging macros
#define LOG_TRACE_FMT(fmt, ...)    Logger::GetInstance().LogFormat(LogLevel::TRACE, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define LOG_DEBUG_FMT(fmt, ...)    Logger::GetInstance().LogFormat(LogLevel::DEBUG, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define LOG_INFO_FMT(fmt, ...)     Logger::GetInstance().LogFormat(LogLevel::INFO, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define LOG_WARNING_FMT(fmt, ...)  Logger::GetInstance().LogFormat(LogLevel::WARNING, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define LOG_ERROR_FMT(fmt, ...)    Logger::GetInstance().LogFormat(LogLevel::ERROR, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define LOG_CRITICAL_FMT(fmt, ...) Logger::GetInstance().LogFormat(LogLevel::CRITICAL, __FILE__, __LINE__, fmt, __VA_ARGS__)
