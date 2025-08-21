#ifndef ASYNC_LOGGER_HPP
#define ASYNC_LOGGER_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

class AsyncLogger {
private:
    std::thread writer_thread;
    std::queue<std::string> log_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::ofstream log_file;
    bool should_stop = false;
    std::string log_path;

    void writer_loop() {
        while (true) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            
            // 等待日志消息或停止信号
            queue_cv.wait(lock, [this] { return !log_queue.empty() || should_stop; });
            
            // 处理所有待写入的日志
            while (!log_queue.empty()) {
                std::string message = log_queue.front();
                log_queue.pop();
                lock.unlock();
                
                if (log_file.is_open()) {
                    log_file << message << std::flush;
                }
                
                lock.lock();
            }
            
            if (should_stop) {
                break;
            }
        }
    }

public:
    AsyncLogger(const std::string& program_name) {
        // 创建程序专用的logs子目录
        std::string program_log_dir = "logs/" + program_name;
        std::filesystem::create_directories(program_log_dir);
        
        // 创建带时间戳的日志文件名
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << program_log_dir << "/" 
           << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
           << "_" << std::setfill('0') << std::setw(3) << ms.count() << ".log";
        
        log_path = ss.str();
        
        // 打开日志文件
        log_file.open(log_path, std::ios::out | std::ios::app);
        if (!log_file.is_open()) {
            std::cerr << "[ERROR] 无法创建日志文件: " << log_path << std::endl;
            return;
        }
        
        // 启动写入线程
        writer_thread = std::thread(&AsyncLogger::writer_loop, this);
        
        // 记录启动信息
        log("INFO", "异步日志系统启动: " + log_path);
    }

    ~AsyncLogger() {
        stop();
    }

    void log(const std::string& level, const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
           << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
           << "[" << level << "] " << message << "\n";
        
        std::lock_guard<std::mutex> lock(queue_mutex);
        log_queue.push(ss.str());
        queue_cv.notify_one();
    }

    void info(const std::string& message) { log("INFO", message); }
    void warn(const std::string& message) { log("WARN", message); }
    void error(const std::string& message) { log("ERROR", message); }
    void debug(const std::string& message) { log("DEBUG", message); }

    std::string get_log_path() const { return log_path; }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            should_stop = true;
        }
        queue_cv.notify_all();
        
        if (writer_thread.joinable()) {
            writer_thread.join();
        }
        
        if (log_file.is_open()) {
            log_file.close();
        }
    }
};

// 全局日志器实例
extern std::unique_ptr<AsyncLogger> g_logger;

// 便捷宏定义
#define LOG_INFO(msg) if (g_logger) g_logger->info(msg)
#define LOG_WARN(msg) if (g_logger) g_logger->warn(msg) 
#define LOG_ERROR(msg) if (g_logger) g_logger->error(msg)
#define LOG_DEBUG(msg) if (g_logger) g_logger->debug(msg)

#endif // ASYNC_LOGGER_HPP
