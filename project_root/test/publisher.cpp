/**
 * @file publisher.cpp
 * @brief 简化的FastDDS消息发布器 - 使用轮询方式发送4个topic
 * 
 * 特点：
 * - 不使用多线程，使用简单的轮询方式
 * - 4个topic轮流发送，总共100Hz
 * - 包含动态变化的真实数据
 * - 支持单次发送和连续发送模式
 * - 参数严格按照IDL定义
 */

#include "fastdds_publisher.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <map>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <signal.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

// 全局发布者实例
std::unique_ptr<FastDDSPublisher> g_publisher = nullptr;
std::atomic<bool> g_running{true};

// 异步日志系统
struct LogEntry {
    std::string topic_path;
    std::string content;
    std::chrono::system_clock::time_point timestamp;
};

class AsyncLogger {
private:
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::thread worker_thread_;
    bool should_stop_ = false;
    std::string session_dir_;
    
public:
    AsyncLogger() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
        session_dir_ = "logs/fastdds/pub/" + ss.str();
        
        std::filesystem::create_directories(session_dir_);
        worker_thread_ = std::thread(&AsyncLogger::worker_loop, this);
        
        std::cout << "日志目录: " << session_dir_ << std::endl;
    }
    
    ~AsyncLogger() {
        stop();
    }
    
    void log(const std::string& topic_path, const std::string& content) {
        LogEntry entry;
        entry.topic_path = topic_path;
        entry.content = content;
        entry.timestamp = std::chrono::system_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            log_queue_.push(entry);
        }
        condition_.notify_one();
    }
    
    void stop() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            should_stop_ = true;
        }
        condition_.notify_all();
        
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
private:
    void worker_loop() {
        while (true) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { return !log_queue_.empty() || should_stop_; });
            
            if (should_stop_ && log_queue_.empty()) {
                break;
            }
            
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
        // 构建完整的日志文件路径
        std::string log_file = session_dir_ + "/" + entry.topic_path + ".log";
        
        // 只创建父目录，不创建topic目录本身
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
};

std::unique_ptr<AsyncLogger> g_logger = nullptr;

/**
 * @brief 获取topic对应的日志路径
 */
std::string get_topic_log_path(const std::string& topic_name) {
    if (topic_name == "handshake_request") {
        return "handshake/request";
    } else if (topic_name == "handshake_response") {
        return "handshake/response";
    } else if (topic_name == "vehicle_status") {
        return "vehicle/vehicle_status";
    } else if (topic_name == "remote_control") {
        return "vehicle/control_cmd";
    }
    return "unknown/" + topic_name;
}

/**
 * @brief 记录发送日志
 */
void log_message_sent(const std::string& topic_name, const std::map<std::string, double>& params) {
    if (!g_logger) return;
    
    std::string topic_path = get_topic_log_path(topic_name);
    
    std::stringstream log_content;
    log_content << "发送成功";
    
    if (!params.empty()) {
        log_content << " - ";
        bool first = true;
        for (const auto& [key, value] : params) {
            if (!first) log_content << ", ";
            log_content << key << "=" << std::fixed << std::setprecision(6) << value;
            first = false;
        }
    }
    
    g_logger->log(topic_path, log_content.str());
}

/**
 * @brief 发送单个消息
 */
bool send_single_message(const std::string& topic_name, const std::map<std::string, double>& params = {}) {
    if (!g_publisher) {
        std::cerr << "发布者未初始化" << std::endl;
        return false;
    }
    
    bool result = false;
    
    if (topic_name == "handshake_request") {
        long noa_active = params.count("noa_active_request") ? static_cast<long>(params.at("noa_active_request")) : 0;
        long override_status = params.count("remote_override_status") ? static_cast<long>(params.at("remote_override_status")) : 0;
        long override_ready = params.count("remote_override_ready") ? static_cast<long>(params.at("remote_override_ready")) : 0;
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        result = g_publisher->publishHandshakeRequest("/handshake/request", noa_active, override_status, override_ready, timestamp);
        
        // 更新参数映射，包含timestamp用于日志记录
        std::map<std::string, double> log_params = params;
        log_params["noa_active_request"] = noa_active;
        log_params["remote_override_status"] = override_status;
        log_params["remote_override_ready"] = override_ready;
        log_params["timestamp"] = static_cast<double>(timestamp);
        if (result) {
            log_message_sent(topic_name, log_params);
        }
        return result;
    } else if (topic_name == "handshake_response") {
        long noa_active = params.count("noa_active_response") ? static_cast<long>(params.at("noa_active_response")) : 0;
        long override_response = params.count("remote_override_response") ? static_cast<long>(params.at("remote_override_response")) : 0;
        bool control_source = params.count("current_control_source") ? static_cast<bool>(params.at("current_control_source")) : false;
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        result = g_publisher->publishHandshakeResponse("/handshake/response", noa_active, override_response, control_source, timestamp);
        
        // 更新参数映射，包含timestamp用于日志记录
        std::map<std::string, double> log_params = params;
        log_params["noa_active_response"] = noa_active;
        log_params["remote_override_response"] = override_response;
        log_params["current_control_source"] = control_source ? 1.0 : 0.0;
        log_params["timestamp"] = static_cast<double>(timestamp);
        if (result) {
            log_message_sent(topic_name, log_params);
        }
        return result;
        
    } else if (topic_name == "vehicle_status") {
        uint32_t vehicle_id = params.count("vehicle_id") ? static_cast<uint32_t>(params.at("vehicle_id")) : 1;
        float speed = params.count("speed") ? static_cast<float>(params.at("speed")) : 0.0f;
        int control_mode = params.count("control_mode") ? static_cast<int>(params.at("control_mode")) : 0;
        float longitude = params.count("position_longitude") ? static_cast<float>(params.at("position_longitude")) : 0.0f;
        float latitude = params.count("position_latitude") ? static_cast<float>(params.at("position_latitude")) : 0.0f;
        
        result = g_publisher->publishVehicleStatus("/vehicle/vehicle_status", vehicle_id, speed, control_mode, longitude, latitude);
        
        // 更新参数映射用于日志记录
        std::map<std::string, double> log_params = params;
        log_params["vehicle_id"] = vehicle_id;
        log_params["speed"] = speed;
        log_params["control_mode"] = control_mode;
        log_params["position_longitude"] = longitude;
        log_params["position_latitude"] = latitude;
        if (result) {
            log_message_sent(topic_name, log_params);
        }
        return result;
        
    } else if (topic_name == "remote_control") {
        bool steering_enable = params.count("steering_angle_enable") ? static_cast<bool>(params.at("steering_angle_enable")) : false;
        float steering_angle = params.count("steering_angle") ? static_cast<float>(params.at("steering_angle")) : 0.0f;
        bool accel_enable = params.count("target_acceleration_enable") ? static_cast<bool>(params.at("target_acceleration_enable")) : false;
        float acceleration = params.count("target_acceleration") ? static_cast<float>(params.at("target_acceleration")) : 0.0f;
        
        result = g_publisher->publishRemoteControl("/vehicle/control_cmd", steering_enable, steering_angle, accel_enable, acceleration);
        
        // 更新参数映射用于日志记录
        std::map<std::string, double> log_params = params;
        log_params["steering_angle_enable"] = steering_enable ? 1.0 : 0.0;
        log_params["steering_angle"] = steering_angle;
        log_params["target_acceleration_enable"] = accel_enable ? 1.0 : 0.0;
        log_params["target_acceleration"] = acceleration;
        if (result) {
            log_message_sent(topic_name, log_params);
        }
        return result;
        
    } else {
        std::cerr << "未知的主题名称: " << topic_name << std::endl;
        return false;
    }
}

/**
 * @brief 精确sleep函数
 */
void precise_sleep_us(int64_t microseconds) {
    auto start = std::chrono::high_resolution_clock::now();
    auto target = start + std::chrono::microseconds(microseconds);
    
    while (std::chrono::high_resolution_clock::now() < target) {
        std::this_thread::yield();
    }
}

/**
 * @brief 信号处理函数
 */
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n\n接收到停止信号，准备退出..." << std::endl;
        g_running = false;
    }
}

/**
 * @brief 连续发送所有topic（简单轮询方式）
 */
void continuous_send_all_topics() {
    std::cout << "开始连续发送所有主题的消息..." << std::endl;
    std::cout << "4个topic轮询发送，总共400Hz，每个topic 100Hz" << std::endl;
    std::cout << "按 Ctrl+C 停止程序" << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 统计变量
    std::map<std::string, int> topic_counts;
    int send_count = 0;
    
    // 动态数据计数器
    uint32_t vehicle_id_counter = 12345;
    float speed_counter = 60.0f;
    float steering_angle_counter = 0.0f;
    float acceleration_counter = 1.0f;
    
    const int64_t TARGET_INTERVAL_US = 2500;   // 2.5ms = 400Hz (每个topic 100Hz)
    
    while (g_running) {
        auto loop_start_time = std::chrono::high_resolution_clock::now();
        
        // 根据发送计数轮询发送不同类型的消息
        int message_type = send_count % 4;
        bool sent = false;
        
        switch(message_type) {
            case 0: {
                // 发送握手请求
                std::map<std::string, double> params = {
                    {"noa_active_request", 1.0},
                    {"remote_override_status", 0.0},
                    {"remote_override_ready", 1.0}
                };
                if (send_single_message("handshake_request", params)) {
                    topic_counts["handshake_request"]++;
                    sent = true;
                }
                break;
            }
            case 1: {
                // 发送车辆状态
                std::map<std::string, double> params = {
                    {"vehicle_id", static_cast<double>(vehicle_id_counter)},
                    {"control_mode", 2.0},
                    {"position_longitude", 116.397},
                    {"position_latitude", 39.916},
                    {"speed", static_cast<double>(speed_counter)},
                    {"gear_position", 1.0},
                    {"heading", 45.0},
                    {"power_mode", 2.0}
                };
                
                // 更新动态数据
                vehicle_id_counter++;
                speed_counter += 0.1f;
                if (speed_counter > 120.0f) speed_counter = 60.0f;
                
                if (send_single_message("vehicle_status", params)) {
                    topic_counts["vehicle_status"]++;
                    sent = true;
                }
                break;
            }
            case 2: {
                // 发送远程控制
                std::map<std::string, double> params = {
                    {"steering_angle_enable", 1.0},
                    {"steering_angle", static_cast<double>(steering_angle_counter)},
                    {"target_acceleration_enable", 1.0},
                    {"target_acceleration", static_cast<double>(acceleration_counter)},
                    {"indicator_left_enable", 0.0},
                    {"indicator_left", 0.0},
                    {"ebrake_status_enable", 1.0},
                    {"ebrake_status", 0.0}
                };
                
                // 更新动态数据
                steering_angle_counter += 0.5f;
                if (steering_angle_counter > 30.0f) steering_angle_counter = -30.0f;
                acceleration_counter += 0.1f;
                if (acceleration_counter > 3.0f) acceleration_counter = 0.5f;
                
                if (send_single_message("remote_control", params)) {
                    topic_counts["remote_control"]++;
                    sent = true;
                }
                break;
            }
            case 3: {
                // 发送握手响应
                std::map<std::string, double> params = {
                    {"noa_active_response", 1.0},
                    {"remote_override_response", 1.0},
                    {"current_control_source", 1.0}
                };
                if (send_single_message("handshake_response", params)) {
                    topic_counts["handshake_response"]++;
                    sent = true;
                }
                break;
            }
        }
        
        if (sent) {
            send_count++;
        }
        
        // 每1000次发送显示统计
        if (send_count % 1000 == 0 && send_count > 0) {
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - start_time).count();
            double actual_freq = (double)send_count * 1000.0 / elapsed_ms;
            
            std::cout << "\n=== 发送统计 (运行时间: " << elapsed_ms << "ms) ===" << std::endl;
            for (const auto& [topic, count] : topic_counts) {
                double frequency = (double)count * 1000.0 / elapsed_ms;
                std::cout << topic << ": " << count << " (" << std::fixed << std::setprecision(5) 
                          << frequency << "Hz)" << std::endl;
            }
            std::cout << "总计: " << send_count << " (" << std::fixed << std::setprecision(4) 
                      << actual_freq << "Hz)" << std::endl;
            std::cout << "==============================" << std::endl;
        }
        
        // 精确时间控制
        auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - loop_start_time).count();
        int64_t sleep_time_us = TARGET_INTERVAL_US - elapsed_time;
        
        if (sleep_time_us > 0) {
            precise_sleep_us(sleep_time_us);
        }
    }
    
    // 输出最终统计
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    double elapsed_seconds = total_elapsed_ms / 1000.0;
    
    std::cout << "\n\n程序结束统计:" << std::endl;
    std::cout << "运行时间: " << std::fixed << std::setprecision(3) << elapsed_seconds << "秒" << std::endl;
    for (const auto& [topic, count] : topic_counts) {
        double frequency = count / elapsed_seconds;
        std::cout << topic << ": " << count << " (" << std::fixed << std::setprecision(2) 
                  << frequency << "Hz)" << std::endl;
    }
}

/**
 * @brief 解析命令行参数并执行单次发送
 */
std::map<std::string, double> parse_params(int argc, char* argv[], int start_idx) {
    std::map<std::string, double> params;
    
    for (int i = start_idx; i < argc; i++) {
        std::string arg = argv[i];
        size_t eq_pos = arg.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = arg.substr(0, eq_pos);
            std::string value_str = arg.substr(eq_pos + 1);
            try {
                double value = std::stod(value_str);
                params[key] = value;
            } catch (const std::exception& e) {
                std::cerr << "warning:无法解析参数 " << arg << ": " << e.what() << std::endl;
            }
        }
    }
    
    return params;
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化日志系统
    g_logger = std::make_unique<AsyncLogger>();
    
    // 初始化发布者
    std::cout << "初始化FastDDS发布者 [域:0]" << std::endl;
    g_publisher = std::make_unique<FastDDSPublisher>();
    if (!g_publisher->init()) {
        std::cerr << "FastDDS发布者初始化失败" << std::endl;
        return 1;
    }
    std::cout << "[SUCCESS] FastDDS发布者初始化成功" << std::endl;
    std::cout << "FastDDS发布者初始化成功" << std::endl;
    
    if (argc == 1) {
        // 无参数：持续发送所有topic
        continuous_send_all_topics();
    } else if (argc >= 2) {
        // 有参数：发送指定topic
        std::string topic_name = argv[1];
        auto params = parse_params(argc, argv, 2);
        
        std::cout << "单次发送模式: " << topic_name << std::endl;
        std::cout << "参数数量: " << params.size() << std::endl;
        
        if (send_single_message(topic_name, params)) {
            std::cout << "消息发送成功" << std::endl;
        } else {
            std::cout << "消息发送失败" << std::endl;
        }
    }
    
    // 清理资源
    std::cout << "清理资源..." << std::endl;
    g_publisher.reset();
    
    std::cout << "等待日志写入完成..." << std::endl;
    g_logger.reset();
    
    return 0;
}