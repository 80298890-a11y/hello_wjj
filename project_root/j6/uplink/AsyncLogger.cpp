#include "AsyncLogger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

AsyncLogger::AsyncLogger() = default;

AsyncLogger::~AsyncLogger() {
    shutdown();
}

bool AsyncLogger::init(const std::string& base_log_dir) {
    m_base_log_dir = base_log_dir;
    
    // 生成会话时间戳
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
    m_session_timestamp = ss.str();
    
    std::cout << "[AsyncLogger] 初始化异步日志系统，会话时间戳: " << m_session_timestamp << std::endl;
    
    // 创建基础日志目录
    try {
        std::filesystem::create_directories(m_base_log_dir + "/" + m_session_timestamp);
    } catch (const std::exception& e) {
        std::cerr << "[AsyncLogger] 创建日志目录失败: " << e.what() << std::endl;
        return false;
    }
    
    // 启动工作线程
    m_running = true;
    m_worker_thread = std::make_unique<std::thread>(&AsyncLogger::workerThread, this);
    
    std::cout << "[AsyncLogger] 异步日志系统启动完成，日志目录: " << m_base_log_dir << "/" << m_session_timestamp << std::endl;
    return true;
}

void AsyncLogger::shutdown() {
    if (m_running) {
        std::cout << "[AsyncLogger] 关闭异步日志系统..." << std::endl;
        
        m_running = false;
        m_queue_cv.notify_all();
        
        if (m_worker_thread && m_worker_thread->joinable()) {
            m_worker_thread->join();
        }
        
        closeAllFiles();
        
        std::cout << "[AsyncLogger] 异步日志系统已关闭，总记录消息数: " << m_total_messages.load() << std::endl;
    }
}

void AsyncLogger::logMessage(const std::string& topic_name, const std::string& message_data, 
                            uint64_t timestamp, uint64_t sequence_id) {
    if (!m_running) return;
    
    LogEntry entry;
    entry.topic_name = topic_name;
    entry.message_data = message_data;
    entry.timestamp = timestamp;
    entry.sequence_id = sequence_id;
    
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        
        // 防止队列过大
        if (m_log_queue.size() >= MAX_QUEUE_SIZE) {
            std::cerr << "[AsyncLogger] 警告: 日志队列已满，丢弃最旧的消息" << std::endl;
            m_log_queue.pop();
        }
        
        m_log_queue.push(std::move(entry));
    }
    
    m_queue_cv.notify_one();
}

void AsyncLogger::workerThread() {
    std::cout << "[AsyncLogger] 日志工作线程启动" << std::endl;
    
    while (m_running) {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        
        // 等待有消息或者停止信号
        m_queue_cv.wait(lock, [this] { 
            return !m_log_queue.empty() || !m_running; 
        });
        
        // 处理队列中的所有消息
        while (!m_log_queue.empty()) {
            LogEntry entry = std::move(m_log_queue.front());
            m_log_queue.pop();
            lock.unlock();
            
            writeLogEntry(entry);
            m_total_messages.fetch_add(1);
            
            lock.lock();
        }
    }
    
    // 处理剩余的消息
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    while (!m_log_queue.empty()) {
        LogEntry entry = std::move(m_log_queue.front());
        m_log_queue.pop();
        writeLogEntry(entry);
        m_total_messages.fetch_add(1);
    }
    
    std::cout << "[AsyncLogger] 日志工作线程结束" << std::endl;
}

std::string AsyncLogger::getLogFilePath(const std::string& topic_name) {
    // 将topic名称中的'/'替换为'_'以适应文件系统
    std::string safe_topic_name = topic_name;
    std::replace(safe_topic_name.begin(), safe_topic_name.end(), '/', '_');
    
    return m_base_log_dir + "/" + m_session_timestamp + "/" + safe_topic_name + ".log";
}

void AsyncLogger::writeLogEntry(const LogEntry& entry) {
    try {
        // 获取或创建对应topic的日志文件
        auto it = m_log_files.find(entry.topic_name);
        if (it == m_log_files.end()) {
            std::string log_file_path = getLogFilePath(entry.topic_name);
            
            // 创建目录
            std::filesystem::path file_path(log_file_path);
            std::filesystem::create_directories(file_path.parent_path());
            
            // 创建文件流
            auto file_stream = std::make_unique<std::ofstream>(log_file_path, std::ios::app);
            if (!file_stream->is_open()) {
                std::cerr << "[AsyncLogger] 无法打开日志文件: " << log_file_path << std::endl;
                return;
            }
            
            // 写入文件头
            *file_stream << "# J6 Message Log File" << std::endl;
            *file_stream << "# Topic: " << entry.topic_name << std::endl;
            *file_stream << "# Session: " << m_session_timestamp << std::endl;
            *file_stream << "# Format: [sequence_id] [timestamp] [message_data]" << std::endl;
            *file_stream << "# =======================================" << std::endl;
            
            std::cout << "[AsyncLogger] 创建新日志文件: " << log_file_path << std::endl;
            
            it = m_log_files.emplace(entry.topic_name, std::move(file_stream)).first;
            m_message_counts[entry.topic_name] = 0;
        }
        
        // 写入日志条目
        auto& file_stream = it->second;
        *file_stream << "[" << entry.sequence_id << "] [" << entry.timestamp << "] " 
                     << entry.message_data << std::endl;
        file_stream->flush();
        
        // 更新计数
        m_message_counts[entry.topic_name]++;
        
        // 每1000条消息打印一次状态
        if (m_message_counts[entry.topic_name] % 1000 == 0) {
            std::cout << "[AsyncLogger] " << entry.topic_name << " 已记录 " 
                     << m_message_counts[entry.topic_name] << " 条消息" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[AsyncLogger] 写入日志失败: " << e.what() << std::endl;
    }
}

void AsyncLogger::closeAllFiles() {
    for (auto& [topic_name, file_stream] : m_log_files) {
        if (file_stream && file_stream->is_open()) {
            std::cout << "[AsyncLogger] 关闭日志文件: " << topic_name 
                     << " (共 " << m_message_counts[topic_name] << " 条消息)" << std::endl;
            file_stream->close();
        }
    }
    m_log_files.clear();
    m_message_counts.clear();
}
