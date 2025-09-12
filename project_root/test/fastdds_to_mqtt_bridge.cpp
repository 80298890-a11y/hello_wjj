/**
 * @file fastdds_to_mqtt_bridge.cpp
 * @brief FastDDS到MQTT桥接业务逻辑程序
 * 
 * 功能：
 * - 订阅FastDDS消息
 * - 转换为JSON格式
 * - 发布到MQTT服务器
 * - 异步日志，按topic分类存储
 * 
 * 使用说明:
 * ./fastdds_to_mqtt_bridge
 */

#include "../fastdds_lib/include/fastdds_subscriber.hpp"
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
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <mosquitto.h>

// 全局订阅者实例
std::unique_ptr<FastDDSSubscriber> g_subscriber = nullptr;
std::atomic<bool> g_running{true};

// 前向声明
void cleanup();

// MQTT相关全局变量
struct mosquitto* g_mosq = nullptr;
const char* mqtt_host = "localhost";  // 连接本地MQTT broker
int mqtt_port = 1883;                 // 本地MQTT端口
const char* client_id = "fastdds_to_mqtt_bridge";

// VID配置
std::string g_vehicle_id = "12345678"; // 默认VID，从配置文件读取

/**
 * @brief 从配置文件读取VID
 */
std::string readVIDFromConfig() {
    const std::string config_file = "/home/wjj/work/car_config/vehicle_config.yaml";
    std::ifstream file(config_file);
    
    if (!file.is_open()) {
        std::cerr << "[ERROR] 无法打开VID配置文件: " << config_file << std::endl;
        return "12345678"; // 返回默认VID
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 查找 vid: "xxxxxxxx" 格式
        size_t vid_pos = line.find("vid:");
        if (vid_pos != std::string::npos) {
            // 提取引号内的内容
            size_t quote_start = line.find("\"", vid_pos);
            if (quote_start != std::string::npos) {
                size_t quote_end = line.find("\"", quote_start + 1);
                if (quote_end != std::string::npos) {
                    std::string vid = line.substr(quote_start + 1, quote_end - quote_start - 1);
                    std::cout << "[SUCCESS] 从配置文件读取VID: " << vid << std::endl;
                    return vid;
                }
            }
        }
    }
    
    std::cerr << "[ERROR] 配置文件中未找到VID，使用默认值" << std::endl;
    return "12345678"; // 返回默认VID
}

/**
 * @brief 生成带VID后缀的MQTT话题
 */
std::string generateTopicWithVID(const std::string& base_topic, const std::string& vid) {
    return base_topic + "/" + vid;
}

/**
 * @brief MQTT发布函数 - 使用libmosquitto库
 */
bool publish_mqtt_message(const std::string& topic, const std::string& message) {
    if (!g_mosq) {
        std::cerr << "MQTT客户端未初始化" << std::endl;
        return false;
    }
    
    // 检查连接状态，如果断开则尝试重连
    int socket = mosquitto_socket(g_mosq);
    if (socket == -1) {
        std::cout << "MQTT连接断开，尝试重连..." << std::endl;
        int reconnect_result = mosquitto_reconnect(g_mosq);
        if (reconnect_result != MOSQ_ERR_SUCCESS) {
            std::cerr << "MQTT重连失败: " << mosquitto_strerror(reconnect_result) << std::endl;
            return false;
        }
        std::cout << "MQTT重连成功" << std::endl;
    }
    
    int result = mosquitto_publish(g_mosq, nullptr, topic.c_str(), 
                                 message.length(), message.c_str(), 0, false);
    
    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "MQTT发布失败: " << mosquitto_strerror(result) << std::endl;
        return false;
    }
    
    // 立即刷新缓存，强制写入网络
    mosquitto_loop_write(g_mosq, 0);
    
    return true;
}

/**
 * @brief 获取当前时间戳
 */
uint64_t getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

/**
 * @brief 初始化MQTT客户端
 */
bool init_mqtt() {
    std::cout << "初始化MQTT客户端..." << std::endl;
    
    // 初始化mosquitto库
    mosquitto_lib_init();
    
    // 创建mosquitto客户端实例
    g_mosq = mosquitto_new(client_id, true, nullptr);
    if (!g_mosq) {
        std::cerr << "创建MQTT客户端失败" << std::endl;
        return false;
    }
    
    // 连接到MQTT服务器
    int result = mosquitto_connect(g_mosq, mqtt_host, mqtt_port, 60);
    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "连接MQTT服务器失败: " << mosquitto_strerror(result) << std::endl;
        return false;
    }
    
    // 启动MQTT网络处理循环
    result = mosquitto_loop_start(g_mosq);
    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "启动MQTT循环失败: " << mosquitto_strerror(result) << std::endl;
        return false;
    }
    
    std::cout << "[SUCCESS] MQTT客户端初始化成功，连接到本地MQTT " << mqtt_host << ":" << mqtt_port << std::endl;
    return true;
}

/**
 * @brief 清理MQTT客户端
 */
void cleanup_mqtt() {
    if (g_mosq) {
        std::cout << "停止MQTT网络循环..." << std::endl;
        mosquitto_loop_stop(g_mosq, true);  // 强制停止
        
        std::cout << "断开MQTT连接..." << std::endl;
        mosquitto_disconnect(g_mosq);
        
        std::cout << "销毁MQTT客户端..." << std::endl;
        mosquitto_destroy(g_mosq);
        g_mosq = nullptr;
    }
    
    std::cout << "清理MQTT库..." << std::endl;
    mosquitto_lib_cleanup();
}

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
            
            queue_cv_.wait(lock, [this] { 
                return !log_queue_.empty() || should_stop_; 
            });
            
            std::vector<LogEntry> batch;
            while (!log_queue_.empty() && batch.size() < 50) {
                batch.push_back(log_queue_.front());
                log_queue_.pop();
            }
            lock.unlock();
            
            for (const auto& entry : batch) {
                write_log_entry(entry);
            }
        }
        
        for (auto& pair : topic_log_files_) {
            if (pair.second.is_open()) {
                pair.second.close();
            }
        }
    }
    
    void write_log_entry(const LogEntry& entry) {
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
            return;
        }
        
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
        
        std::filesystem::create_directories(topic_dir);
        
        std::string log_path = topic_dir + "/" + log_filename;
        topic_log_paths_[topic_name] = log_path;
        topic_log_files_[topic_name].open(log_path);
        
        if (topic_log_files_[topic_name].is_open()) {
            std::cout << "创建Bridge日志文件: " << log_path << std::endl;
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
 * @brief FastDDS消息回调处理器
 */
class MessageHandler {
public:
    /**
     * @brief 处理HandshakeRequest消息
     */
    static void on_handshake_request(long noa_active, long override_status, long override_ready, uint64_t timestamp) {
        uint64_t received_timestamp = getCurrentTimestamp();
        std::string mqtt_message = "{\"noa_active\":" + std::to_string(noa_active) + 
                                   ",\"override_status\":" + std::to_string(override_status) + 
                                   ",\"override_ready\":" + std::to_string(override_ready) + 
                                   ",\"fastdds_timestamp\":" + std::to_string(timestamp) +
                                   ",\"received_timestamp\":" + std::to_string(received_timestamp) + "}";
        
        std::string topic = generateTopicWithVID("/handshake/request", g_vehicle_id);
        bool success = publish_mqtt_message(topic, mqtt_message);
        
        std::ostringstream oss;
        oss << "FastDDS->MQTT " << (success ? "成功" : "失败") 
            << " [" << topic << "] " << mqtt_message;
        
        if (g_logger) {
            g_logger->log("handshake_request", oss.str());
        }
        
        if (success) {
            std::cout << "[SUCCESS] HandshakeRequest转发成功" << std::endl;
        } else {
            std::cout << "[ERROR] HandshakeRequest转发失败" << std::endl;
        }
    }
    
    /**
     * @brief 处理HandshakeResponse消息
     */
    static void on_handshake_response(long noa_active, long override_response, bool control_source, uint64_t timestamp) {
        uint64_t received_timestamp = getCurrentTimestamp();
        std::string mqtt_message = std::string("{\"noa_active\":") + std::to_string(noa_active) + 
                                   ",\"override_response\":" + std::to_string(override_response) + 
                                   ",\"control_source\":" + (control_source ? "true" : "false") + 
                                   ",\"fastdds_timestamp\":" + std::to_string(timestamp) +
                                   ",\"received_timestamp\":" + std::to_string(received_timestamp) + "}";
        
        std::string topic = generateTopicWithVID("/handshake/response", g_vehicle_id);
        bool success = publish_mqtt_message(topic, mqtt_message);
        
        std::ostringstream oss;
        oss << "FastDDS->MQTT " << (success ? "成功" : "失败") 
            << " [" << topic << "] " << mqtt_message;
        
        if (g_logger) {
            g_logger->log("handshake_response", oss.str());
        }
    }
    
    /**
     * @brief 处理VehicleStatus消息 - 接收所有16个字段
     */
    static void on_vehicle_status(uint32_t vehicle_id, int control_mode, float position_longitude, float position_latitude, float position_altitude, float speed, float yawrate, float gear_position, float acceleration, float heading, float steering_angle, float wheel_angle, long ebrake_status, long indicator_left, long indicator_right, uint64_t timestamp, int power_mode) {
        std::string mqtt_message = "{\"vehicle_id\":" + std::to_string(vehicle_id) +
                                   ",\"control_mode\":" + std::to_string(control_mode) +
                                   ",\"position_longitude\":" + std::to_string(position_longitude) +
                                   ",\"position_latitude\":" + std::to_string(position_latitude) +
                                   ",\"position_altitude\":" + std::to_string(position_altitude) +
                                   ",\"speed\":" + std::to_string(speed) +
                                   ",\"yawrate\":" + std::to_string(yawrate) +
                                   ",\"gear_position\":" + std::to_string(gear_position) +
                                   ",\"acceleration\":" + std::to_string(acceleration) +
                                   ",\"heading\":" + std::to_string(heading) +
                                   ",\"steering_angle\":" + std::to_string(steering_angle) +
                                   ",\"wheel_angle\":" + std::to_string(wheel_angle) +
                                   ",\"ebrake_status\":" + std::to_string(ebrake_status) +
                                   ",\"indicator_left\":" + std::to_string(indicator_left) +
                                   ",\"indicator_right\":" + std::to_string(indicator_right) +
                                   ",\"timestamp\":" + std::to_string(timestamp) +
                                   ",\"power_mode\":" + std::to_string(power_mode) + "}";
        
        std::string topic = generateTopicWithVID("/vehicle/vehicle_status", g_vehicle_id);
        bool success = publish_mqtt_message(topic, mqtt_message);
        
        std::ostringstream oss;
        oss << "FastDDS->MQTT " << (success ? "成功" : "失败") 
            << " [" << topic << "] " << mqtt_message;
        
        if (g_logger) {
            g_logger->log("vehicle_status", oss.str());
        }
        
        if (success) {
            std::cout << "[SUCCESS] VehicleStatus转发成功" << std::endl;
        } else {
            std::cout << "[ERROR] VehicleStatus转发失败" << std::endl;
        }
    }
    
    /**
     * @brief 处理RemoteControl消息 - 接收所有13个字段
     */
    static void on_remote_control(bool steering_angle_enable, float steering_angle, bool target_acceleration_enable, float target_acceleration, bool indicator_left_enable, bool indicator_left, bool indicator_right_enable, bool indicator_right, bool gear_position_enable, long gear_position, bool ebrake_status_enable, long ebrake_status, uint64_t timestamp) {
        std::string mqtt_message = std::string("{\"steering_angle_enable\":") + (steering_angle_enable ? "true" : "false") + 
                                   ",\"steering_angle\":" + std::to_string(steering_angle) + 
                                   ",\"target_acceleration_enable\":" + (target_acceleration_enable ? "true" : "false") + 
                                   ",\"target_acceleration\":" + std::to_string(target_acceleration) + 
                                   ",\"indicator_left_enable\":" + (indicator_left_enable ? "true" : "false") + 
                                   ",\"indicator_left\":" + (indicator_left ? "true" : "false") + 
                                   ",\"indicator_right_enable\":" + (indicator_right_enable ? "true" : "false") + 
                                   ",\"indicator_right\":" + (indicator_right ? "true" : "false") + 
                                   ",\"gear_position_enable\":" + (gear_position_enable ? "true" : "false") + 
                                   ",\"gear_position\":" + std::to_string(gear_position) + 
                                   ",\"ebrake_status_enable\":" + (ebrake_status_enable ? "true" : "false") + 
                                   ",\"ebrake_status\":" + std::to_string(ebrake_status) + 
                                   ",\"timestamp\":" + std::to_string(timestamp) + "}";
        
        std::string topic = generateTopicWithVID("/vehicle/control_cmd", g_vehicle_id);
        bool success = publish_mqtt_message(topic, mqtt_message);
        
        std::ostringstream oss;
        oss << "FastDDS->MQTT " << (success ? "成功" : "失败") 
            << " [" << topic << "] " << mqtt_message;
        
        if (g_logger) {
            g_logger->log("remote_control", oss.str());
        }
        
        if (success) {
            std::cout << "[SUCCESS] RemoteControl转发成功" << std::endl;
        } else {
            std::cout << "[ERROR] RemoteControl转发失败" << std::endl;
        }
    }
};

/**
 * @brief 创建日志目录
 */
std::string create_log_directory() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    
    std::string log_dir = "logs/fastdds_mqtt/fastdds_to_mqtt/" + oss.str();
    std::filesystem::create_directories(log_dir);
    
    return log_dir;
}

/**
 * @brief 初始化订阅者
 */
bool init_subscriber() {
    std::cout << "初始化FastDDS订阅者 [域:0]" << std::endl;
    
    g_subscriber = std::make_unique<FastDDSSubscriber>(0);
    
    if (!g_subscriber) {
        std::cerr << "创建订阅者失败" << std::endl;
        return false;
    }
    
    // 设置消息回调处理器
    g_subscriber->setHandshakeRequestCallback(MessageHandler::on_handshake_request);
    g_subscriber->setHandshakeResponseCallback(MessageHandler::on_handshake_response);
    g_subscriber->setVehicleStatusCallback(MessageHandler::on_vehicle_status);
    g_subscriber->setRemoteControlCallback(MessageHandler::on_remote_control);
    
    // 初始化订阅者并订阅所有主题
    if (!g_subscriber->init()) {
        std::cerr << "FastDDS订阅者初始化失败" << std::endl;
        return false;
    }
    
    std::cout << "FastDDS订阅者初始化成功" << std::endl;
    return true;
}

/**
 * @brief 清理资源
 */
void cleanup() {
    static bool cleanup_called = false;
    if (cleanup_called) return;
    cleanup_called = true;
    
    std::cout << "\n程序正在退出..." << std::endl;
    
    // 1. 首先清理MQTT客户端（停止网络线程）
    cleanup_mqtt();
    
    // 2. 清理FastDDS订阅者
    if (g_subscriber) {
        std::cout << "清理FastDDS订阅者..." << std::endl;
        g_subscriber.reset();
    }
    
    // 3. 最后停止异步日志
    if (g_logger) {
        std::cout << "等待Bridge日志写入完成..." << std::endl;
        g_logger->stop();
        
        // 显示日志文件路径
        std::cout << "Bridge日志文件已保存到:" << std::endl;
        for (const auto& pair : g_logger->get_log_paths()) {
            std::cout << "  " << pair.first << ": " << pair.second << std::endl;
        }
        
        g_logger.reset();
    }
    
    std::cout << "[SUCCESS] 清理完成" << std::endl;
}

int main() {
    std::cout << "FastDDS到MQTT桥接器 (反向链路-本地)" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "接收FastDDS消息并转发到本地MQTT (localhost:1883)" << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 读取VID配置
    g_vehicle_id = readVIDFromConfig();
    std::cout << "[SUCCESS] 当前车辆VID: " << g_vehicle_id << std::endl;

    // 创建日志目录并初始化异步日志
    std::string log_dir = create_log_directory();
    std::cout << "Bridge日志目录: " << log_dir << std::endl;
    g_logger = std::make_unique<AsyncLogger>(log_dir);

    // 初始化MQTT客户端
    if (!init_mqtt()) {
        cleanup();
        return 1;
    }

    if (!init_subscriber()) {
        cleanup();
        return 1;
    }

    std::cout << "\n开始监听FastDDS消息..." << std::endl;
    std::cout << "按 Ctrl+C 停止程序" << std::endl;

    // 主循环
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    cleanup();
    return 0;
}
