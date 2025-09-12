#pragma once

#include <string>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <atomic>
#include <map>
#include <iomanip>
#include <sstream>

/**
 * @brief 通用日志管理器 - 支持自动清理和配置化
 * 
 * 特性：
 * - 基于YAML配置文件的日志管理
 * - 自动清理超过指定大小的日志文件
 * - 异步日志写入，不阻塞主线程
 * - 支持多种日志级别
 * - 线程安全
 */
class UniversalLogger {
public:
    enum class LogLevel {
        DEBUG = 0,
        INFO = 1,
        WARN = 2,
        ERROR = 3
    };

    struct LogConfig {
        std::string base_dir = "logs";
        std::string subdir;
        int max_storage_mb = 50;
        int max_files = 20;
        LogLevel level = LogLevel::INFO;
        bool auto_cleanup = true;
        int cleanup_interval_minutes = 10;
        bool disable_program_logs = false;
    };

    struct LogEntry {
        LogLevel level;
        std::string message;
        std::chrono::system_clock::time_point timestamp;
        std::string topic_file; // Topic专用文件路径 (可选)
    };

private:
    LogConfig config_;
    std::string program_name_;
    std::string log_file_path_;
    std::ofstream log_file_;
    std::string session_timestamp_;  // 会话级时间戳，用于topic日志目录
    
    // 异步日志队列
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_thread_;
    std::atomic<bool> should_stop_{false};
    
    // 清理线程
    std::thread cleanup_thread_;
    std::chrono::system_clock::time_point last_cleanup_;

public:
    /**
     * @brief 构造函数
     * @param program_name 程序名称，用于查找配置
     */
    explicit UniversalLogger(const std::string& program_name);
    
    /**
     * @brief 析构函数
     */
    ~UniversalLogger();

    /**
     * @brief 记录日志
     * @param level 日志级别
     * @param message 消息内容
     */
    void log(LogLevel level, const std::string& message);
    
    /**
     * @brief 便捷方法 - INFO级别
     */
    void info(const std::string& message) { log(LogLevel::INFO, message); }
    
    /**
     * @brief 便捷方法 - DEBUG级别
     */
    void debug(const std::string& message) { log(LogLevel::DEBUG, message); }
    
    /**
     * @brief 便捷方法 - WARN级别
     */
    void warn(const std::string& message) { log(LogLevel::WARN, message); }
    
    /**
     * @brief 便捷方法 - ERROR级别
     */
    void error(const std::string& message) { log(LogLevel::ERROR, message); }

    /**
     * @brief 按Topic记录日志 - INFO级别
     * @param topic_path Topic路径 (如 "handshake/request", "vehicle/control_cmd")
     * @param message 消息内容
     */
    void logByTopic(const std::string& topic_path, const std::string& message);

    /**
     * @brief 获取日志目录路径
     */
    std::string getLogDir() const;

private:
    /**
     * @brief 加载配置文件
     */
    void loadConfig();
    
    /**
     * @brief 创建日志文件路径
     */
    void createLogFile();
    
    /**
     * @brief 日志工作线程
     */
    void workerLoop();
    
    /**
     * @brief 清理工作线程
     */
    void cleanupLoop();
    
    /**
     * @brief 清理旧日志文件
     */
    void cleanupOldLogs();
    
    /**
     * @brief 获取目录大小(MB)
     */
    double getDirectorySizeMB(const std::string& dir_path);
    
    /**
     * @brief 日志级别转字符串
     */
    std::string levelToString(LogLevel level);
    
    /**
     * @brief 格式化时间戳
     */
    std::string formatTimestamp(const std::chrono::system_clock::time_point& tp);
};

/**
 * @brief 全局日志宏定义
 */
extern std::unique_ptr<UniversalLogger> g_universal_logger;

#define LOG_INFO(msg) if(g_universal_logger) g_universal_logger->info(msg)
#define LOG_DEBUG(msg) if(g_universal_logger) g_universal_logger->debug(msg)
#define LOG_WARN(msg) if(g_universal_logger) g_universal_logger->warn(msg)
#define LOG_ERROR(msg) if(g_universal_logger) g_universal_logger->error(msg)
#define LOG_BY_TOPIC(topic_path, msg) if(g_universal_logger) g_universal_logger->logByTopic(topic_path, msg)

/**
 * @brief 初始化全局日志器
 * @param program_name 程序名称
 */
void initUniversalLogger(const std::string& program_name);

/**
 * @brief 清理全局日志器
 */
void cleanupUniversalLogger();
