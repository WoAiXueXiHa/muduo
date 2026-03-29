#ifndef MUDUO_LOGGING_HPP
#define MUDUO_LOGGING_HPP

#include <iostream>
#include <mutex>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>

namespace muduo {

enum LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void log(LogLevel level, const char* file, int line, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
            << "." << std::setfill('0') << std::setw(3) << ms.count()
            << " [" << levelStr(level) << "]"
            << " [" << std::this_thread::get_id() << "]"
            << " " << file << ":" << line
            << " " << msg;
        
        std::cout << oss.str() << std::endl;
    }

private:
    Logger() = default;
    std::mutex mutex_;

    const char* levelStr(LogLevel level) {
        switch (level) {
            case DEBUG: return "DEBUG";
            case INFO:  return "INFO ";
            case WARN:  return "WARN ";
            case ERROR: return "ERROR";
            default:    return "?????";
        }
    }
};

} // namespace muduo

#include <cstdio>
#include <cstring>

// 支持 printf 风格格式化：LOG_INFO("fd=%d err=%s", fd, strerror(errno))
#define LOG_DEBUG(fmt, ...) do { \
    char _log_buf[1024]; \
    snprintf(_log_buf, sizeof(_log_buf), fmt, ##__VA_ARGS__); \
    muduo::Logger::instance().log(muduo::DEBUG, __FILE__, __LINE__, _log_buf); \
} while(0)

#define LOG_INFO(fmt, ...) do { \
    char _log_buf[1024]; \
    snprintf(_log_buf, sizeof(_log_buf), fmt, ##__VA_ARGS__); \
    muduo::Logger::instance().log(muduo::INFO, __FILE__, __LINE__, _log_buf); \
} while(0)

#define LOG_WARN(fmt, ...) do { \
    char _log_buf[1024]; \
    snprintf(_log_buf, sizeof(_log_buf), fmt, ##__VA_ARGS__); \
    muduo::Logger::instance().log(muduo::WARN, __FILE__, __LINE__, _log_buf); \
} while(0)

#define LOG_ERROR(fmt, ...) do { \
    char _log_buf[1024]; \
    snprintf(_log_buf, sizeof(_log_buf), fmt, ##__VA_ARGS__); \
    muduo::Logger::instance().log(muduo::ERROR, __FILE__, __LINE__, _log_buf); \
} while(0)

#endif
