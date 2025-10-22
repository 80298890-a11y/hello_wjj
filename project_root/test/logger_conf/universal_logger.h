#pragma once

#include <string>
#include <memory>
#include <map>
#include <mutex>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>

/**
 * @brief 基于spdlog的通用日志管理器 - 支持自动清理和配置化
 * 
 * 特性：
 * - 基于YAML配置文件的日志管理
 * - 自动清理超过指定大小的日志文件（通过rotating_file_sink实现）
 * - 异步日志写入，不阻塞主线程
 * - 支持多种日志级别
 * - 线程安全
 * - 按topic分类存储日志
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
     * @brief 创建主日志器
     */
    void createMainLogger();
    
    /**
     * @brief 获取或创建topic日志器
     */
    std::shared_ptr<spdlog::logger> getOrCreateTopicLogger(const std::string& topic_path);
    
    /**
     * @brief 转换日志级别
     */
    spdlog::level::level_enum toSpdlogLevel(LogLevel level);
    
    /**
     * @brief 生成分钟级时间戳
     */
    std::string getMinuteTimestamp() const;
    
    /**
     * @brief 计算单个日志文件的最大大小（字节）
     */
    size_t calculateMaxFileSize() const;

private:
    LogConfig config_;
    std::string program_name_;
    std::string log_dir_;
    std::string session_timestamp_;
    
    // 主程序日志器
    std::shared_ptr<spdlog::logger> main_logger_;
    
    // Topic日志器缓存
    std::map<std::string, std::shared_ptr<spdlog::logger>> topic_loggers_;
    std::mutex topic_loggers_mutex_;
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

