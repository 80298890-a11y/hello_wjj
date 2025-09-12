#include "universal_logger.h"
#include <iostream>
#include <algorithm>
#include <vector>

// 全局日志器实例
std::unique_ptr<UniversalLogger> g_universal_logger = nullptr;

// 简化的YAML解析（仅解析我们需要的格式）
class SimpleYAMLParser {
public:
    std::map<std::string, std::string> parseFile(const std::string& filename) {
        std::map<std::string, std::string> result;
        std::ifstream file(filename);
        
        if (!file.is_open()) {
            return result; // 返回空map，使用默认配置
        }
        
        std::string line;
        std::string current_section;
        std::string current_program;
        
        while (std::getline(file, line)) {
            // 去除前后空格
            line = trim(line);
            
            // 跳过注释和空行
            if (line.empty() || line[0] == '#') continue;
            
            // 检查是否是section
            if (line.find(':') != std::string::npos && line.back() != ':') {
                auto pos = line.find(':');
                std::string key = trim(line.substr(0, pos));
                std::string value = trim(line.substr(pos + 1));
                
                // 移除引号
                if (value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.length() - 2);
                }
                
                // 构建完整key
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
    : program_name_(program_name)
    , last_cleanup_(std::chrono::system_clock::now()) {
    
    loadConfig();
    createLogFile();
    
    // 启动工作线程
    worker_thread_ = std::thread(&UniversalLogger::workerLoop, this);
    
    // 启动清理线程（如果开启自动清理）
    if (config_.auto_cleanup) {
        cleanup_thread_ = std::thread(&UniversalLogger::cleanupLoop, this);
    }
    
    // 记录启动信息
    info("通用日志系统启动: " + log_file_path_);
}

UniversalLogger::~UniversalLogger() {
    should_stop_ = true;
    queue_cv_.notify_all();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    if (log_file_.is_open()) {
        log_file_.close();
    }
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
    
    // 如果没有配置文件，尝试上级目录
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
        if (config_map.count(prog_prefix + "subdir")) {
            config_.subdir = config_map[prog_prefix + "subdir"];
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
}

void UniversalLogger::createLogFile() {
    // 创建日志目录
    std::string log_dir = config_.base_dir + "/" + config_.subdir;
    std::filesystem::create_directories(log_dir);
    
    // 生成时间戳文件名
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") 
        << "_" << std::setfill('0') << std::setw(3) << ms.count() << ".log";
    
    log_file_path_ = log_dir + "/" + oss.str();
    
    // 初始化session级时间戳（用于topic分类日志目录）
    std::ostringstream session_oss;
    session_oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") 
               << "_" << std::setfill('0') << std::setw(3) << ms.count();
    session_timestamp_ = session_oss.str();
    
    // 只有在未禁用程序级日志时才创建日志文件
    if (!config_.disable_program_logs) {
        log_file_.open(log_file_path_, std::ios::app);
        if (!log_file_.is_open()) {
            std::cerr << "无法创建日志文件: " << log_file_path_ << std::endl;
        }
    }
}

void UniversalLogger::log(LogLevel level, const std::string& message) {
    // 检查日志级别
    if (level < config_.level) {
        return;
    }
    
    LogEntry entry;
    entry.level = level;
    entry.message = message;
    entry.timestamp = std::chrono::system_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        log_queue_.push(entry);
    }
    queue_cv_.notify_one();
}

void UniversalLogger::workerLoop() {
    while (!should_stop_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] { return !log_queue_.empty() || should_stop_; });
        
        while (!log_queue_.empty()) {
            LogEntry entry = log_queue_.front();
            log_queue_.pop();
            lock.unlock();
            
            // 写入日志
            if (!entry.topic_file.empty()) {
                // 写入到Topic专用文件
                std::ofstream topic_file(entry.topic_file, std::ios::app);
                if (topic_file.is_open()) {
                    topic_file << "[" << formatTimestamp(entry.timestamp) << "] "
                              << "[" << levelToString(entry.level) << "] "
                              << entry.message << std::endl;
                    topic_file.close();
                }
            } else {
                // 写入到默认文件 (仅在未禁用程序级日志时)
                if (log_file_.is_open() && !config_.disable_program_logs) {
                    log_file_ << "[" << formatTimestamp(entry.timestamp) << "] "
                             << "[" << levelToString(entry.level) << "] "
                             << entry.message << std::endl;
                    log_file_.flush();
                }
            }
            
            lock.lock();
        }
    }
}

void UniversalLogger::cleanupLoop() {
    while (!should_stop_) {
        auto now = std::chrono::system_clock::now();
        auto since_last = std::chrono::duration_cast<std::chrono::minutes>(now - last_cleanup_).count();
        
        if (since_last >= config_.cleanup_interval_minutes) {
            cleanupOldLogs();
            last_cleanup_ = now;
        }
        
        // 每秒检查一次停止标志，避免长时间阻塞
        for (int i = 0; i < 60 && !should_stop_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void UniversalLogger::cleanupOldLogs() {
    std::string log_dir = config_.base_dir + "/" + config_.subdir;
    
    if (!std::filesystem::exists(log_dir)) {
        return;
    }
    
    // 获取当前目录大小
    double current_size_mb = getDirectorySizeMB(log_dir);
    
    if (current_size_mb <= config_.max_storage_mb) {
        return; // 不需要清理
    }
    
    // 收集所有日志文件
    std::vector<std::pair<std::filesystem::file_time_type, std::string>> log_files;
    
    for (const auto& entry : std::filesystem::directory_iterator(log_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            log_files.emplace_back(entry.last_write_time(), entry.path().string());
        }
    }
    
    // 按时间排序（最旧的在前）
    std::sort(log_files.begin(), log_files.end());
    
    // 删除最旧的文件，直到满足大小限制
    size_t deleted_count = 0;
    for (const auto& [time, filepath] : log_files) {
        // 不删除当前正在使用的日志文件
        if (filepath == log_file_path_) {
            continue;
        }
        
        try {
            std::filesystem::remove(filepath);
            deleted_count++;
            
            // 重新检查大小
            current_size_mb = getDirectorySizeMB(log_dir);
            if (current_size_mb <= config_.max_storage_mb) {
                break;
            }
        } catch (const std::exception& e) {
            std::cerr << "删除日志文件失败: " << filepath << " - " << e.what() << std::endl;
        }
    }
    
    if (deleted_count > 0) {
        info("自动清理: 删除了 " + std::to_string(deleted_count) + 
             " 个旧日志文件，当前大小: " + std::to_string(current_size_mb) + "MB");
    }
}

double UniversalLogger::getDirectorySizeMB(const std::string& dir_path) {
    size_t total_size = 0;
    
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir_path)) {
            if (entry.is_regular_file()) {
                total_size += entry.file_size();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "计算目录大小失败: " << dir_path << " - " << e.what() << std::endl;
        return 0.0;
    }
    
    return static_cast<double>(total_size) / (1024.0 * 1024.0);
}

std::string UniversalLogger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string UniversalLogger::formatTimestamp(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
        << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

void UniversalLogger::logByTopic(const std::string& topic_path, const std::string& message) {
    // 创建topic专用的日志文件路径 - 使用session级时间戳
    std::string topic_log_dir = config_.base_dir + "/" + config_.subdir + "/" + session_timestamp_;
    std::string topic_log_file = topic_log_dir + "/" + topic_path + ".log";
    
    // 确保目录存在
    std::filesystem::create_directories(std::filesystem::path(topic_log_file).parent_path());
    
    // 创建日志条目
    LogEntry entry;
    entry.level = LogLevel::INFO;
    entry.message = message;
    entry.timestamp = std::chrono::system_clock::now();
    entry.topic_file = topic_log_file;  // 使用topic专用文件
    
    // 线程安全地添加到队列
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        log_queue_.push(entry);
    }
    queue_cv_.notify_one();
}

std::string UniversalLogger::getLogDir() const {
    return config_.base_dir + "/" + config_.subdir;
}

// 全局函数实现
void initUniversalLogger(const std::string& program_name) {
    g_universal_logger = std::make_unique<UniversalLogger>(program_name);
}

void cleanupUniversalLogger() {
    g_universal_logger.reset();
}
