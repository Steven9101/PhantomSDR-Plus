#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <fstream>
#include <mutex>
#include <memory>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

class Logger {
private:
    static std::unique_ptr<Logger> instance;
    static std::mutex mutex;
    
    std::ofstream file_stream;
    bool log_to_file;
    bool log_to_console;
    LogLevel min_level;
    
    // ANSI color codes
    const std::string RESET = "\033[0m";
    const std::string BOLD = "\033[1m";
    const std::string RED = "\033[31m";
    const std::string GREEN = "\033[32m";
    const std::string YELLOW = "\033[33m";
    const std::string BLUE = "\033[34m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN = "\033[36m";
    const std::string WHITE = "\033[37m";
    const std::string GRAY = "\033[90m";
    
    Logger() : log_to_file(false), log_to_console(true), min_level(LogLevel::INFO) {}
    
    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    
    std::string level_to_string(LogLevel level) {
        switch(level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARNING: return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::CRITICAL: return "CRIT";
            default: return "UNKNOWN";
        }
    }
    
    std::string get_color_for_level(LogLevel level) {
        switch(level) {
            case LogLevel::DEBUG: return GRAY;
            case LogLevel::INFO: return GREEN;
            case LogLevel::WARNING: return YELLOW;
            case LogLevel::ERROR: return RED;
            case LogLevel::CRITICAL: return BOLD + RED;
            default: return WHITE;
        }
    }
    
public:
    static Logger& getInstance() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!instance) {
            instance.reset(new Logger());
        }
        return *instance;
    }
    
    bool set_log_file(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex);
        if (file_stream.is_open()) {
            file_stream.close();
        }
        file_stream.open(filename, std::ios::app);
        log_to_file = file_stream.is_open();
        if (log_to_file) {
            log_unlocked(LogLevel::INFO, "Logger", "Logging to file: " + filename);
        }
        return log_to_file;
    }
    
    void set_console_logging(bool enable) {
        log_to_console = enable;
    }
    
    void set_min_level(LogLevel level) {
        min_level = level;
    }
    
private:
    void log_unlocked(LogLevel level, const std::string& component, const std::string& message) {
        if (level < min_level) return;
        
        std::string timestamp = get_timestamp();
        std::string level_str = level_to_string(level);
        
        // Format: [TIMESTAMP] [LEVEL] [COMPONENT] Message
        std::stringstream log_line;
        log_line << "[" << timestamp << "] [" << level_str << "] [" << component << "] " << message;
        
        // Log to file (without colors)
        if (log_to_file && file_stream.is_open()) {
            file_stream << log_line.str() << std::endl;
            file_stream.flush();
        }
        
        // Log to console (with colors)
        if (log_to_console) {
            std::cout << GRAY << "[" << timestamp << "] "
                      << get_color_for_level(level) << "[" << level_str << "] "
                      << CYAN << "[" << component << "] "
                      << RESET << message << std::endl;
        }
    }
    
public:
    void log(LogLevel level, const std::string& component, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex);
        log_unlocked(level, component, message);
    }
    
    // Convenience methods
    void debug(const std::string& component, const std::string& message) {
        log(LogLevel::DEBUG, component, message);
    }
    
    void info(const std::string& component, const std::string& message) {
        log(LogLevel::INFO, component, message);
    }
    
    void warning(const std::string& component, const std::string& message) {
        log(LogLevel::WARNING, component, message);
    }
    
    void error(const std::string& component, const std::string& message) {
        log(LogLevel::ERROR, component, message);
    }
    
    void critical(const std::string& component, const std::string& message) {
        log(LogLevel::CRITICAL, component, message);
    }
    
    // Special method for startup banner (always shows on console)
    void banner(const std::string& message) {
        if (log_to_console) {
            std::cout << message << std::endl;
        }
        if (log_to_file && file_stream.is_open()) {
            // Strip ANSI codes for file
            std::string clean_message = message;
            size_t pos = 0;
            while ((pos = clean_message.find("\033[")) != std::string::npos) {
                size_t end = clean_message.find("m", pos);
                if (end != std::string::npos) {
                    clean_message.erase(pos, end - pos + 1);
                } else {
                    break;
                }
            }
            file_stream << clean_message << std::endl;
            file_stream.flush();
        }
    }
    
    ~Logger() {
        if (file_stream.is_open()) {
            file_stream.close();
        }
    }
};

// Static initialization - inline for C++17
inline std::unique_ptr<Logger> Logger::instance = nullptr;
inline std::mutex Logger::mutex;

// Convenience macro
#define LOG_DEBUG(component, message) Logger::getInstance().debug(component, message)
#define LOG_INFO(component, message) Logger::getInstance().info(component, message)
#define LOG_WARNING(component, message) Logger::getInstance().warning(component, message)
#define LOG_ERROR(component, message) Logger::getInstance().error(component, message)
#define LOG_CRITICAL(component, message) Logger::getInstance().critical(component, message)

#endif // LOGGER_H