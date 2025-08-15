/**
 * @file subscriber.cpp
 * @brief FastDDS接收业务逻辑程序
 * 
 * 功能：
 * - 订阅所有4个FastDDS topic
 * - 记录接收到的消息
 * - 统计接收频率
 * - 异步日志，按topic分类存储
 * 
 * 使用说明：
 * ./subscriber
 */

#include "../fastdds_lib/include/fastdds_subscriber.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <queue>

// 全局控制变量
std::unique_ptr<FastDDSSubscriber> g_subscriber = nullptr;
std::atomic<bool> g_running{true};

// 统计计数器
std::atomic<uint64_t> g_handshake_request_count{0};
std::atomic<uint64_t> g_handshake_response_count{0};
std::atomic<uint64_t> g_vehicle_status_count{0};
std::atomic<uint64_t> g_remote_control_count{0};

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
    std::map<std::string, std::ofstream> topic_log_files_;
    
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_thread_;
    std::atomic<bool> should_stop_{false};
    
    void worker_loop() {
        while (!should_stop_ || !log_queue_.empty()) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // 等待有日志或者停止信号
            queue_cv_.wait(lock, [this] { 
                return !log_queue_.empty() || should_stop_; 
            });
            
            // 批量处理日志
            std::vector<LogEntry> batch;
            while (!log_queue_.empty() && batch.size() < 50) {
                batch.push_back(log_queue_.front());
                log_queue_.pop();
            }
            lock.unlock();
            
            // 写入日志
            for (const auto& entry : batch) {
                write_log_entry(entry);
            }
        }
        
        // 关闭所有文件
        for (auto& pair : topic_log_files_) {
            if (pair.second.is_open()) {
                pair.second.close();
            }
        }
    }
    
    void write_log_entry(const LogEntry& entry) {
        // 确保topic日志文件已创建
        ensure_topic_log_file(entry.topic_name);
        
        auto& log_file = topic_log_files_[entry.topic_name];
        if (log_file.is_open()) {
            auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                entry.timestamp.time_since_epoch()) % 1000;
            
            log_file << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                     << "." << std::setfill('0') << std::setw(3) << ms.count()
                     << "] " << entry.content << std::endl;
            log_file.flush();
        }
    }
    
    void ensure_topic_log_file(const std::string& topic_name) {
        if (topic_log_files_.find(topic_name) != topic_log_files_.end()) {
            return; // 已经创建
        }
        
        // 创建topic专用目录和日志文件
        std::string topic_dir;
        std::string log_filename;
        
        if (topic_name == "handshake_request") {
            topic_dir = base_log_dir_ + "/handshake";
            log_filename = "request.log";
        } else if (topic_name == "handshake_response") {
            topic_dir = base_log_dir_ + "/handshake";
            log_filename = "response.log";
        } else if (topic_name == "vehicle_status") {
            topic_dir = base_log_dir_ + "/vehicle";
            log_filename = "vehicle_status.log";
        } else if (topic_name == "remote_control") {
            topic_dir = base_log_dir_ + "/vehicle";
            log_filename = "control_cmd.log";
        } else {
            topic_dir = base_log_dir_ + "/other";
            log_filename = topic_name + ".log";
        }
        
        // 创建目录
        std::filesystem::create_directories(topic_dir);
        
        // 创建并打开日志文件
        std::string log_path = topic_dir + "/" + log_filename;
        topic_log_paths_[topic_name] = log_path;
        topic_log_files_[topic_name].open(log_path);
        
        if (topic_log_files_[topic_name].is_open()) {
            std::cout << "创建日志文件: " << log_path << std::endl;
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
        should_stop_ = true;
        queue_cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
    std::map<std::string, std::string> get_log_paths() const {
        return topic_log_paths_;
    }
};

// 全局异步日志器
std::unique_ptr<AsyncLogger> g_logger = nullptr;

/**
 * @brief 信号处理函数
 */
void signal_handler(int signal) {
    static bool signal_received = false;
    if (signal_received) return;
    signal_received = true;
    
    std::cout << "\n接收到停止信号，准备退出..." << std::endl;
    g_running = false;
}

/**
 * @brief 创建日志目录
 */
std::string create_log_directory() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    
    std::string log_dir = "logs/fastdds/sub/" + oss.str();
    std::filesystem::create_directories(log_dir);
    
    return log_dir;
}

/**
 * @brief HandshakeRequest回调
 */
void on_handshake_request(long noa_active, long override_status, long override_ready, uint64_t timestamp) {
    g_handshake_request_count++;
    
    std::ostringstream oss;
    oss << "接收 - noa_active=" << noa_active 
        << ", override_status=" << override_status 
        << ", override_ready=" << override_ready
        << ", timestamp=" << timestamp;
    
    if (g_logger) {
        g_logger->log("handshake_request", oss.str());
    }
}

/**
 * @brief HandshakeResponse回调
 */
void on_handshake_response(long noa_active, long override_response, bool control_source, uint64_t timestamp) {
    g_handshake_response_count++;
    
    std::ostringstream oss;
    oss << "接收 - noa_active=" << noa_active 
        << ", override_response=" << override_response 
        << ", control_source=" << (control_source ? "true" : "false")
        << ", timestamp=" << timestamp;
    
    if (g_logger) {
        g_logger->log("handshake_response", oss.str());
    }
}

/**
 * @brief VehicleStatus回调 - 接收所有16个字段
 */
void on_vehicle_status(uint32_t vehicle_id, int control_mode, float position_longitude, float position_latitude, float position_altitude, float speed, float yawrate, float gear_position, float acceleration, float heading, float steering_angle, float wheel_angle, long ebrake_status, long indicator_left, long indicator_right, uint64_t timestamp, int power_mode) {
    g_vehicle_status_count++;
    
    std::ostringstream oss;
    oss << "接收 - vehicle_id=" << vehicle_id 
        << ", control_mode=" << control_mode 
        << ", position_longitude=" << position_longitude 
        << ", position_latitude=" << position_latitude 
        << ", position_altitude=" << position_altitude 
        << ", speed=" << speed 
        << ", yawrate=" << yawrate 
        << ", gear_position=" << gear_position 
        << ", acceleration=" << acceleration 
        << ", heading=" << heading 
        << ", steering_angle=" << steering_angle 
        << ", wheel_angle=" << wheel_angle 
        << ", ebrake_status=" << ebrake_status 
        << ", indicator_left=" << indicator_left 
        << ", indicator_right=" << indicator_right 
        << ", timestamp=" << timestamp 
        << ", power_mode=" << power_mode;
    
    if (g_logger) {
        g_logger->log("vehicle_status", oss.str());
    }
}

/**
 * @brief RemoteControl回调 - 接收所有13个字段
 */
void on_remote_control(bool steering_angle_enable, float steering_angle, bool target_acceleration_enable, float target_acceleration, bool indicator_left_enable, bool indicator_left, bool indicator_right_enable, bool indicator_right, bool gear_position_enable, long gear_position, bool ebrake_status_enable, long ebrake_status, uint64_t timestamp) {
    g_remote_control_count++;
    
    std::ostringstream oss;
    oss << "接收 - steering_angle_enable=" << (steering_angle_enable ? "true" : "false")
        << ", steering_angle=" << steering_angle 
        << ", target_acceleration_enable=" << (target_acceleration_enable ? "true" : "false")
        << ", target_acceleration=" << target_acceleration
        << ", indicator_left_enable=" << (indicator_left_enable ? "true" : "false")
        << ", indicator_left=" << (indicator_left ? "true" : "false")
        << ", indicator_right_enable=" << (indicator_right_enable ? "true" : "false")
        << ", indicator_right=" << (indicator_right ? "true" : "false")
        << ", gear_position_enable=" << (gear_position_enable ? "true" : "false")
        << ", gear_position=" << gear_position
        << ", ebrake_status_enable=" << (ebrake_status_enable ? "true" : "false")
        << ", ebrake_status=" << ebrake_status
        << ", timestamp=" << timestamp;
    
    if (g_logger) {
        g_logger->log("remote_control", oss.str());
    }
}

/**
 * @brief 初始化订阅者
 */
bool init_subscriber() {
    std::cout << "初始化FastDDS订阅者 [域:0]" << std::endl;
    
    g_subscriber = std::make_unique<FastDDSSubscriber>(0);
    if (!g_subscriber) {
        std::cerr << "错误: 创建订阅者失败" << std::endl;
        return false;
    }
    
    // 设置回调函数
    g_subscriber->setHandshakeRequestCallback(on_handshake_request);
    g_subscriber->setHandshakeResponseCallback(on_handshake_response);
    g_subscriber->setVehicleStatusCallback(on_vehicle_status);
    g_subscriber->setRemoteControlCallback(on_remote_control);
    
    if (!g_subscriber->init()) {
        std::cerr << "错误: 订阅者初始化失败" << std::endl;
        return false;
    }
    
    std::cout << "FastDDS订阅者初始化成功" << std::endl;
    return true;
}

/**
 * @brief 统计显示线程
 */
void statistics_thread() {
    auto start_time = std::chrono::steady_clock::now();
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        if (!g_running) break;
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        
        uint64_t hr_count = g_handshake_request_count.load();
        uint64_t hrs_count = g_handshake_response_count.load();
        uint64_t vs_count = g_vehicle_status_count.load();
        uint64_t rc_count = g_remote_control_count.load();
        uint64_t total = hr_count + hrs_count + vs_count + rc_count;
        
        std::cout << "\n=== 接收统计 (运行时间: " << elapsed << "ms) ===" << std::endl;
        std::cout << "HandshakeRequest: " << hr_count 
                  << " (" << (hr_count * 1000.0 / elapsed) << "Hz)" << std::endl;
        std::cout << "HandshakeResponse: " << hrs_count 
                  << " (" << (hrs_count * 1000.0 / elapsed) << "Hz)" << std::endl;
        std::cout << "VehicleStatus: " << vs_count 
                  << " (" << (vs_count * 1000.0 / elapsed) << "Hz)" << std::endl;
        std::cout << "RemoteControl: " << rc_count 
                  << " (" << (rc_count * 1000.0 / elapsed) << "Hz)" << std::endl;
        std::cout << "总计: " << total 
                  << " (" << (total * 1000.0 / elapsed) << "Hz)" << std::endl;
        std::cout << "==============================\n" << std::endl;
    }
}

/**
 * @brief 清理资源
 */
void cleanup() {
    std::cout << "清理资源..." << std::endl;
    
    // 停止异步日志
    if (g_logger) {
        std::cout << "等待日志写入完成..." << std::endl;
        g_logger->stop();
        
        // 显示日志文件路径
        std::cout << "日志文件已保存到:" << std::endl;
        for (const auto& pair : g_logger->get_log_paths()) {
            std::cout << "  " << pair.first << ": " << pair.second << std::endl;
        }
        
        g_logger.reset();
    }
    
    if (g_subscriber) {
        g_subscriber.reset();
    }
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 创建日志目录并初始化异步日志
    std::string log_dir = create_log_directory();
    std::cout << "日志目录: " << log_dir << std::endl;
    g_logger = std::make_unique<AsyncLogger>(log_dir);
    
    // 初始化订阅者
    if (!init_subscriber()) {
        cleanup();
        return -1;
    }
    
    std::cout << "开始监听FastDDS消息..." << std::endl;
    std::cout << "按 Ctrl+C 停止程序" << std::endl;
    
    // 启动统计线程
    std::thread stats_thread(statistics_thread);
    
    // 主循环
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 等待统计线程结束
    if (stats_thread.joinable()) {
        stats_thread.join();
    }
    
    cleanup();
    return 0;
}