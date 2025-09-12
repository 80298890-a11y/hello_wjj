#pragma once

#include <string>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <fstream>
#include <chrono>
#include <unordered_map>
#include <filesystem>

struct LogEntry {
    std::string topic_name;
    std::string message_data;
    uint64_t timestamp;
    uint64_t sequence_id;
};

class AsyncLogger {
public:
    AsyncLogger();
    ~AsyncLogger();
    
    bool init(const std::string& base_log_dir = "logs");
    void shutdown();
    
    // 异步记录消息
    void logMessage(const std::string& topic_name, const std::string& message_data, 
                   uint64_t timestamp, uint64_t sequence_id);
    
    // 获取统计信息
    uint64_t getTotalMessages() const { return m_total_messages.load(); }
    uint64_t getQueueSize() const { return m_log_queue.size(); }
    
private:
    void workerThread();
    std::string getLogFilePath(const std::string& topic_name);
    void writeLogEntry(const LogEntry& entry);
    void closeAllFiles();
    
private:
    std::atomic<bool> m_running{false};
    std::unique_ptr<std::thread> m_worker_thread;
    
    std::queue<LogEntry> m_log_queue;
    mutable std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;
    
    std::string m_base_log_dir;
    std::string m_session_timestamp;
    std::unordered_map<std::string, std::unique_ptr<std::ofstream>> m_log_files;
    std::unordered_map<std::string, uint64_t> m_message_counts;
    
    std::atomic<uint64_t> m_total_messages{0};
    
    static constexpr size_t MAX_QUEUE_SIZE = 10000;
};
