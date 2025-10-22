#include "universal_logger.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>

// 全局日志器实例
std::unique_ptr<UniversalLogger> g_universal_logger = nullptr;

// 简化的YAML解析（保持与原版一致）
class SimpleYAMLParser {
public:
    std::map<std::string, std::string> parseFile(const std::string& filename) {
        std::map<std::string, std::string> result;
        std::ifstream file(filename);
        
        if (!file.is_open()) {
            return result;
        }
        
        std::string line;
        std::string current_section;
        std::string current_program;
        
        while (std::getline(file, line)) {
            line = trim(line);
            
            if (line.empty() || line[0] == '#') continue;
            
            if (line.find(':') != std::string::npos && line.back() != ':') {
                auto pos = line.find(':');
                std::string key = trim(line.substr(0, pos));
                std::string value = trim(line.substr(pos + 1));
                
                if (value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.length() - 2);
                }
                
                std::string full_key = current_section;
                if (!current_program.empty()) {
                    full_key += "." + current_program;
                }
                if (!full_key.empty()) {
                    full_key += "." + key;
                } else {
                    full_key = key;
                }
                
                result[full_key] = value;
            } else if (line.back() == ':') {
                std::string section = line.substr(0, line.length() - 1);
                section = trim(section);
                
                if (current_section == "programs") {
                    current_program = section;
                } else {
                    current_section = section;
                    current_program = "";
                }
            }
        }
        
        file.close();
        return result;
    }
    
private:
    std::string trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }
};

UniversalLogger::UniversalLogger(const std::string& program_name)
    : program_name_(program_name) {
    
    // 初始化spdlog异步日志
    spdlog::init_thread_pool(8192, 1);  // 队列大小8192，1个后台线程
    
    loadConfig();
    
    // 生成session时间戳
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::ostringstream session_oss;
    session_oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") 
               << "_" << std::setfill('0') << std::setw(3) << ms.count();
    session_timestamp_ = session_oss.str();
    
    createMainLogger();
    
    if (main_logger_) {
        main_logger_->info("通用日志系统启动 (基于spdlog)");
    }
}

UniversalLogger::~UniversalLogger() {
    // 清理所有topic loggers
    {
        std::lock_guard<std::mutex> lock(topic_loggers_mutex_);
        for (auto& [topic, logger] : topic_loggers_) {
            logger->flush();
            spdlog::drop(logger->name());
        }
        topic_loggers_.clear();
    }
    
    // 清理主logger
    if (main_logger_) {
        main_logger_->flush();
        spdlog::drop(main_logger_->name());
    }
    
    // 关闭spdlog
    spdlog::shutdown();
}

void UniversalLogger::loadConfig() {
    // 设置默认配置
    config_.base_dir = "logs";
    config_.subdir = program_name_;
    config_.max_storage_mb = 50;
    config_.max_files = 20;
    config_.level = LogLevel::INFO;
    config_.auto_cleanup = true;
    config_.cleanup_interval_minutes = 10;
    
    // 尝试加载配置文件
    SimpleYAMLParser parser;
    auto config_map = parser.parseFile("logger_conf/log_config.yaml");
    
    if (config_map.empty()) {
        config_map = parser.parseFile("../logger_conf/log_config.yaml");
    }
    
    if (!config_map.empty()) {
        // 加载全局配置
        if (config_map.count("global.max_storage_mb")) {
            config_.max_storage_mb = std::stoi(config_map["global.max_storage_mb"]);
        }
        if (config_map.count("global.base_dir")) {
            config_.base_dir = config_map["global.base_dir"];
        }
        if (config_map.count("global.auto_cleanup")) {
            config_.auto_cleanup = (config_map["global.auto_cleanup"] == "true");
        }
        if (config_map.count("global.cleanup_interval_minutes")) {
            config_.cleanup_interval_minutes = std::stoi(config_map["global.cleanup_interval_minutes"]);
        }
        if (config_map.count("global.disable_program_logs")) {
            config_.disable_program_logs = (config_map["global.disable_program_logs"] == "true");
        }
        
        // 加载程序特定配置
        std::string prog_prefix = "programs." + program_name_ + ".";
        bool has_program_config = config_map.count(prog_prefix + "subdir") > 0;
        
        if (has_program_config) {
            config_.subdir = config_map[prog_prefix + "subdir"];
            // 如果程序有特定配置，默认启用该程序的日志（除非显式配置了disable_program_logs）
            config_.disable_program_logs = false;
        }
        
        // 程序级配置可以显式覆盖disable_program_logs
        if (config_map.count(prog_prefix + "disable_program_logs")) {
            config_.disable_program_logs = (config_map[prog_prefix + "disable_program_logs"] == "true");
        }
        
        if (config_map.count(prog_prefix + "max_files")) {
            config_.max_files = std::stoi(config_map[prog_prefix + "max_files"]);
        }
        if (config_map.count(prog_prefix + "level")) {
            std::string level_str = config_map[prog_prefix + "level"];
            if (level_str == "DEBUG") config_.level = LogLevel::DEBUG;
            else if (level_str == "INFO") config_.level = LogLevel::INFO;
            else if (level_str == "WARN") config_.level = LogLevel::WARN;
            else if (level_str == "ERROR") config_.level = LogLevel::ERROR;
        }
    }
    
    log_dir_ = config_.base_dir + "/" + config_.subdir;
}

void UniversalLogger::createMainLogger() {
    if (config_.disable_program_logs) {
        // 如果禁用程序级日志，不创建主logger
        main_logger_ = nullptr;
        return;
    }
    
    // 创建日志目录
    std::filesystem::create_directories(log_dir_);
    
    // 计算每个日志文件的最大大小
    size_t max_file_size = calculateMaxFileSize();
    
    // 生成日志文件名
    std::string log_filename = log_dir_ + "/" + session_timestamp_ + ".log";
    
    try {
        // 创建rotating file sink
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_filename, 
            max_file_size,      // 每个文件的最大大小
            config_.max_files   // 最多保留的文件数
        );
        
        // 设置日志格式: [时间戳] [级别] 消息
        sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        
        // 创建异步logger
        main_logger_ = std::make_shared<spdlog::async_logger>(
            program_name_,
            sink,
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block
        );
        
        // 设置日志级别
        main_logger_->set_level(toSpdlogLevel(config_.level));
        
        // 设置刷新策略（每条日志都刷新）
        main_logger_->flush_on(spdlog::level::trace);
        
        // 注册到spdlog
        spdlog::register_logger(main_logger_);
        
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "创建主日志器失败: " << ex.what() << std::endl;
        main_logger_ = nullptr;
    }
}

std::shared_ptr<spdlog::logger> UniversalLogger::getOrCreateTopicLogger(const std::string& topic_path) {
    std::lock_guard<std::mutex> lock(topic_loggers_mutex_);
    
    // 使用分钟级时间戳作为key
    std::string minute_timestamp = getMinuteTimestamp();
    std::string logger_key = topic_path + "_" + minute_timestamp;
    
    // 检查是否已存在
    auto it = topic_loggers_.find(logger_key);
    if (it != topic_loggers_.end()) {
        return it->second;
    }
    
    // 创建新的topic logger
    std::string topic_log_dir = log_dir_ + "/" + minute_timestamp;
    std::filesystem::create_directories(topic_log_dir);
    
    std::string log_filename = topic_log_dir + "/" + topic_path + ".log";
    
    try {
        // 为topic日志创建单独的sink（不限制大小，因为按分钟轮转）
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_filename,
            1024 * 1024 * 1024,  // 1GB (实际上按时间轮转，这个大小基本不会达到)
            1                     // 只保留1个文件（因为我们按分钟创建新logger）
        );
        
        sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        
        // 创建异步logger
        auto logger = std::make_shared<spdlog::async_logger>(
            logger_key,
            sink,
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block
        );
        
        logger->set_level(spdlog::level::info);  // topic日志总是INFO级别
        logger->flush_on(spdlog::level::trace);
        
        spdlog::register_logger(logger);
        
        // 缓存logger
        topic_loggers_[logger_key] = logger;
        
        return logger;
        
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "创建topic日志器失败 [" << topic_path << "]: " << ex.what() << std::endl;
        return nullptr;
    }
}

void UniversalLogger::log(LogLevel level, const std::string& message) {
    if (!main_logger_) {
        return;
    }
    
    switch (level) {
        case LogLevel::DEBUG:
            main_logger_->debug(message);
            break;
        case LogLevel::INFO:
            main_logger_->info(message);
            break;
        case LogLevel::WARN:
            main_logger_->warn(message);
            break;
        case LogLevel::ERROR:
            main_logger_->error(message);
            break;
    }
}

void UniversalLogger::logByTopic(const std::string& topic_path, const std::string& message) {
    // 同时记录到主日志和topic日志
    // 主日志：便于快速查看所有消息流
    if (main_logger_) {
        main_logger_->info(message);
    }
    
    // Topic日志：按主题分类存储
    auto logger = getOrCreateTopicLogger(topic_path);
    if (logger) {
        logger->info(message);
    }
}

std::string UniversalLogger::getLogDir() const {
    return log_dir_;
}

spdlog::level::level_enum UniversalLogger::toSpdlogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return spdlog::level::debug;
        case LogLevel::INFO: return spdlog::level::info;
        case LogLevel::WARN: return spdlog::level::warn;
        case LogLevel::ERROR: return spdlog::level::err;
        default: return spdlog::level::info;
    }
}

std::string UniversalLogger::getMinuteTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M");
    
    return oss.str();
}

size_t UniversalLogger::calculateMaxFileSize() const {
    // 将配置的总大小MB转换为每个文件的大小
    // 考虑到max_files的数量，平均分配
    size_t total_bytes = static_cast<size_t>(config_.max_storage_mb) * 1024 * 1024;
    size_t max_file_size = total_bytes / config_.max_files;
    
    // 确保至少1MB
    if (max_file_size < 1024 * 1024) {
        max_file_size = 1024 * 1024;
    }
    
    return max_file_size;
}

// 全局函数实现
void initUniversalLogger(const std::string& program_name) {
    g_universal_logger = std::make_unique<UniversalLogger>(program_name);
}

void cleanupUniversalLogger() {
    g_universal_logger.reset();
}

