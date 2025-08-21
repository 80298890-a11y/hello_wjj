/**
 * @file mqtt_to_fastdds_bridge.cpp
 * @brief MQTT到FastDDS桥接业务逻辑程序
 * 
 * 功能：
 * - 订阅MQTT消息（来自mqtt_sender）
 * - 解析JSON数据
 * - 转换为FastDDS消息并发布
 * - 异步日志，按topic分类存储
 * 
 * 使用说明：
 * ./mqtt_to_fastdds_bridge
 */

#include "../fastdds_lib/include/mqtt_to_fastdds_converter.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <atomic>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <map>

// 全局控制变量
std::unique_ptr<MQTTToFastDDSConverter> g_converter = nullptr;
std::atomic<bool> g_running{true};

// 前向声明
void cleanup();

// 异步日志系统
struct LogEntry {
    std::string topic_name;
    std::string content;
    std::chrono::system_clock::time_point timestamp;
};

class AsyncLogger {
private:
    std::string base_log_dir_;
    std::map<std::string, std::string> topic_log_paths_;
    
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_thread_;
    std::atomic<bool> stop_flag_{false};

    void worker_loop() {
        while (!stop_flag_) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !log_queue_.empty() || stop_flag_; });
            
            while (!log_queue_.empty()) {
                LogEntry entry = log_queue_.front();
                log_queue_.pop();
                lock.unlock();
                
                write_log(entry);
                
                lock.lock();
            }
        }
    }
    
    void write_log(const LogEntry& entry) {
        // 构建日志文件路径: logs/fastdds_mqtt/mqtt_to_fastdds/{timestamp}/{topic_group}/{topic_name}.log
        std::string log_file = base_log_dir_ + "/" + entry.topic_name + ".log";
        
        // 创建父目录
        std::filesystem::path log_path(log_file);
        std::filesystem::create_directories(log_path.parent_path());
        
        std::ofstream file(log_file, std::ios::app);
        if (file.is_open()) {
            auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                entry.timestamp.time_since_epoch()) % 1000;
            
            file << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
            file << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
            file << entry.content << std::endl;
            file.close();
        }
    }

public:
    AsyncLogger(const std::string& base_dir) : base_log_dir_(base_dir) {
        worker_thread_ = std::thread(&AsyncLogger::worker_loop, this);
    }
    
    ~AsyncLogger() {
        stop();
    }
    
    void log(const std::string& topic_name, const std::string& content) {
        LogEntry entry;
        entry.topic_name = topic_name;
        entry.content = content;
        entry.timestamp = std::chrono::system_clock::now();
        
        std::lock_guard<std::mutex> lock(queue_mutex_);
        log_queue_.push(entry);
        queue_cv_.notify_one();
    }
    
    void stop() {
        stop_flag_ = true;
        queue_cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
    std::map<std::string, std::string> get_log_paths() const {
        return topic_log_paths_;
    }
};

std::unique_ptr<AsyncLogger> g_logger = nullptr;

/**
 * @brief 创建日志目录
 */
std::string create_log_directory() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    
    std::string log_dir = "logs/fastdds_mqtt/mqtt_to_fastdds/" + oss.str();
    std::filesystem::create_directories(log_dir);
    
    return log_dir;
}

/**
 * @brief 获取topic对应的日志路径
 */
std::string get_topic_log_path(const std::string& topic_name) {
    if (topic_name == "/handshake/request") {
        return "handshake/request";
    } else if (topic_name == "/handshake/response") {
        return "handshake/response";
    } else if (topic_name == "/vehicle/vehicle_status") {
        return "vehicle/vehicle_status";
    } else if (topic_name == "/vehicle/control_cmd") {
        return "vehicle/control_cmd";
    }
    return "unknown/" + topic_name;
}

/**
 * @brief MQTT消息回调处理器
 */
void on_mqtt_message_received(const std::string& topic, const std::string& message, bool conversion_success) {
    // 统计转换结果
    static std::map<std::string, int> success_count;
    static std::map<std::string, int> total_count;
    
    total_count[topic]++;
    
    // 根据转换结果更新成功计数
    if (conversion_success) {
        success_count[topic]++;
    }
    
    // 记录接收到MQTT消息的日志
    std::string topic_path = get_topic_log_path(topic);
    std::ostringstream log_content;
    log_content << "MQTT接收成功 [" << topic << "] → FastDDS转换" 
                << (conversion_success ? "成功" : "失败") << ": " << message;
    
    if (g_logger) {
        g_logger->log(topic_path, log_content.str());
    }
    
    // 显示统计信息（每10条消息显示一次）
    if (total_count[topic] % 10 == 0) {
        std::cout << "\n=== 转换统计 [" << topic << "] ===" << std::endl;
        std::cout << "总计: " << total_count[topic] << " 条" << std::endl;
        std::cout << "成功: " << success_count[topic] << " 条" << std::endl;
        std::cout << "失败: " << (total_count[topic] - success_count[topic]) << " 条" << std::endl;
        std::cout << "==========================================\n" << std::endl;
    }
}

/**
 * @brief 信号处理函数
 */
void signal_handler(int signal) {
    static int signal_count = 0;
    signal_count++;
    
    if (signal_count == 1) {
        std::cout << "\n\n接收到停止信号(" << signal << ")，准备优雅退出..." << std::endl;
        g_running = false;
    } else if (signal_count >= 2) {
        std::cout << "\n\n接收到第二次信号，强制退出..." << std::endl;
        cleanup();
        exit(1);
    }
}

/**
 * @brief 清理资源
 */
void cleanup() {
    static bool cleanup_called = false;
    if (cleanup_called) return;
    cleanup_called = true;
    
    std::cout << "\n程序正在退出..." << std::endl;
    
    // 1. 清理转换器（包含MQTT客户端和FastDDS发布者）
    if (g_converter) {
        std::cout << "清理MQTT到FastDDS转换器..." << std::endl;
        g_converter.reset();
    }
    
    // 2. 停止异步日志
    if (g_logger) {
        std::cout << "等待日志写入完成..." << std::endl;
        g_logger->stop();
        
        // 显示日志文件路径
        std::cout << "桥接器日志文件已保存到:" << std::endl;
        for (const auto& pair : g_logger->get_log_paths()) {
            std::cout << "  " << pair.first << ": " << pair.second << std::endl;
        }
        
        g_logger.reset();
    }
    
    std::cout << "清理完成" << std::endl;
}

/**
 * @brief 显示转换器状态
 */
void display_converter_status() {
    if (g_converter) {
        std::cout << "MQTT状态: " << g_converter->getMQTTStatusString() << std::endl;
        std::cout << "FastDDS状态: " << (g_converter->isMQTTConnected() ? "已初始化" : "未初始化") << std::endl;
    }
}

int main() {
    std::cout << "MQTT到FastDDS桥接器" << std::endl;
    std::cout << "===================" << std::endl;
    std::cout << "订阅MQTT消息并转换为FastDDS消息 (hellorobotaxi.cn:11883 → 本地FastDDS域)" << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 创建日志目录并初始化异步日志
    std::string log_dir = create_log_directory();
    std::cout << "桥接器日志目录: " << log_dir << std::endl;
    g_logger = std::make_unique<AsyncLogger>(log_dir);

    // 创建MQTT到FastDDS转换器
    g_converter = std::make_unique<MQTTToFastDDSConverter>(0, "hellorobotaxi.cn", 11883, "mqtt_to_fastdds_bridge");
    
    // 设置MQTT消息回调
    g_converter->setMQTTMessageCallback(on_mqtt_message_received);

    // 初始化转换器
    if (!g_converter->init()) {
        std::cerr << "[ERROR] MQTT到FastDDS转换器初始化失败" << std::endl;
        cleanup();
        return 1;
    }

    std::cout << "\n开始监听MQTT消息并转换为FastDDS..." << std::endl;
    std::cout << "订阅的MQTT主题:" << std::endl;
    std::cout << "  - /handshake/request" << std::endl;
    std::cout << "  - /handshake/response" << std::endl;
    std::cout << "  - /vehicle/vehicle_status" << std::endl;
    std::cout << "  - /vehicle/control_cmd" << std::endl;
    
    // 额外等待确保所有订阅都已建立
    std::cout << "\n等待MQTT订阅完全建立..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "[READY] 桥接器准备完毕，可以接收MQTT消息" << std::endl;
    std::cout << "\n按 Ctrl+C 停止程序" << std::endl;

    // 显示初始状态
    display_converter_status();

    // 主循环
    auto last_status_time = std::chrono::steady_clock::now();
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 每30秒显示一次状态
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_status_time).count() >= 30) {
            std::cout << "\n=== 桥接器状态 ===" << std::endl;
            display_converter_status();
            std::cout << "==================" << std::endl;
            last_status_time = now;
        }
    }

    cleanup();
    return 0;
}
