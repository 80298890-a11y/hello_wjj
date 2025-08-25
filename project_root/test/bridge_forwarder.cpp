/**
 * @file bridge_forwarder.cpp
 * @brief 双向桥接转发器 - 支持正向和反向链路
 * 
 * 正向链路: 订阅/handshake/request/noa → 400Hz聚合发送4个IDL
 * 反向链路: 订阅本地MQTT带VID话题 → 转发到外部服务器
 */

#include <iostream>
#include <mosquitto.h>
#include <json/json.h>
#include <chrono>
#include <thread>
#include <string>
#include <mutex>
#include <signal.h>
#include <memory>
#include <map>
#include <vector>
#include <fstream>
#include "async_logger.hpp"

// 全局变量
static bool running = true;
std::unique_ptr<AsyncLogger> g_logger;

// MQTT客户端 - 双向连接
struct mosquitto* external_mosq = nullptr;  // 连接外部服务器 (hellorobotaxi.cn:11883)
struct mosquitto* local_mosq = nullptr;     // 连接本地MQTT (localhost:1883)

// 服务器配置
const std::string EXTERNAL_HOST = "hellorobotaxi.cn";
const int EXTERNAL_PORT = 11883;
const std::string LOCAL_HOST = "localhost";
const int LOCAL_PORT = 1883;

// VID配置
std::string g_vehicle_id = "12345678";

// 正向链路状态管理
static std::mutex g_forward_mutex;
static std::map<std::string, double> g_handshake_state = {
    {"noa_active_request", 1.0}, {"remote_override_status", 0.0}, {"remote_override_ready", 1.0}
};

// IDL默认值 - 包含所有字段的完整默认值
static const std::map<std::string, std::map<std::string, double>> g_idl_defaults = {
    {"/handshake/request", {
        {"noa_active_request", 1.0}, 
        {"remote_override_status", 0.0}, 
        {"remote_override_ready", 1.0}
    }},
    {"/handshake/response", {
        {"noa_active_response", 1.0}, 
        {"remote_override_response", 0.0}, 
        {"current_control_source", 1.0}
    }},
    {"/vehicle/vehicle_status", {
        {"vehicle_id", 12345.0}, 
        {"control_mode", 2.0}, 
        {"position_longitude", 116.4074}, 
        {"position_latitude", 39.9042}, 
        {"position_altitude", 50.0}, 
        {"speed", 60.5}, 
        {"yawrate", 0.0}, 
        {"gear_position", 3.0}, 
        {"acceleration", 0.0}, 
        {"heading", 0.0}, 
        {"steering_angle", 0.0}, 
        {"wheel_angle", 0.0}, 
        {"ebrake_status", 0.0}, 
        {"indicator_left", 0.0}, 
        {"indicator_right", 0.0}, 
        {"power_mode", 2.0}
    }},
    {"/vehicle/control_cmd", {
        {"steering_angle_enable", 0.0}, 
        {"steering_angle", 15.5}, 
        {"target_acceleration_enable", 0.0}, 
        {"target_acceleration", 0.0}, 
        {"indicator_left_enable", 0.0}, 
        {"indicator_left", 0.0}, 
        {"indicator_right_enable", 0.0}, 
        {"indicator_right", 0.0}, 
        {"gear_position_enable", 1.0}, 
        {"gear_position", 1.0}, 
        {"ebrake_status_enable", 1.0}, 
        {"ebrake_status", 0.0}
    }}
};

/**
 * @brief 从配置文件读取VID
 */
std::string readVIDFromConfig() {
    const std::string config_file = "../../car_config/vehicle_config.yaml";
    std::ifstream file(config_file);
    
    if (!file.is_open()) {
        return "12345678";
    }
    
    std::string line;
    while (std::getline(file, line)) {
        size_t vid_pos = line.find("vid:");
        if (vid_pos != std::string::npos) {
            size_t quote_start = line.find("\"", vid_pos);
            if (quote_start != std::string::npos) {
                size_t quote_end = line.find("\"", quote_start + 1);
                if (quote_end != std::string::npos) {
                    std::string vid = line.substr(quote_start + 1, quote_end - quote_start - 1);
                    std::cout << "[SUCCESS] 读取VID: " << vid << std::endl;
                    return vid;
                }
            }
        }
    }
    
    return "12345678";
}

/**
 * @brief 获取当前时间戳
 */
uint64_t getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

/**
 * @brief 构建JSON消息
 */
std::string buildJsonMessage(const std::string& topic, const std::map<std::string, double>& values) {
    Json::Value json_obj;
    for (const auto& [key, value] : values) {
        json_obj[key] = value;
    }
    json_obj["timestamp"] = static_cast<Json::Int64>(getCurrentTimestamp());
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, json_obj);
}

/**
 * @brief 发布消息到外部MQTT服务器
 */
bool publishToExternal(const std::string& topic, const std::string& message) {
    if (!external_mosq) return false;
    
    int rc = mosquitto_publish(external_mosq, nullptr, topic.c_str(), 
                              message.length(), message.c_str(), 0, false);
    return rc == MOSQ_ERR_SUCCESS;
}

// =========================== 正向链路逻辑 ===========================

/**
 * @brief 正向链路: 400Hz发送线程
 */
void forwardLinkThread() {
    std::cout << "[SUCCESS] 正向链路启动: 400Hz轮流发送4个IDL" << std::endl;
    
    std::vector<std::string> topics = {
        "/handshake/request", "/handshake/response", 
        "/vehicle/vehicle_status", "/vehicle/control_cmd"
    };
    
    uint64_t message_count = 0;
    const auto interval = std::chrono::microseconds(2500); // 400Hz = 2.5ms间隔
    
    while (running) {
        auto loop_start = std::chrono::high_resolution_clock::now();
        
        // 轮流发送4个IDL
        std::string topic = topics[message_count % 4];
        
        // 获取当前状态 - 所有IDL都使用完整的默认值，然后根据实际状态更新
        std::map<std::string, double> current_state;
        {
            std::lock_guard<std::mutex> lock(g_forward_mutex);
            current_state = g_idl_defaults.at(topic); // 先使用完整默认值
            
            // 如果是handshake/request，则更新实际接收到的状态
            if (topic == "/handshake/request") {
                for (const auto& [key, value] : g_handshake_state) {
                    current_state[key] = value; // 覆盖对应字段
                }
            }
        }
        
        // 发送消息
        std::string message = buildJsonMessage(topic, current_state);
        bool success = publishToExternal(topic, message);
        
        if (success) {
            message_count++;
            if (message_count % 400 == 0) { // 每秒显示一次统计
                g_logger->log("INFO", "正向链路: 已发送 " + std::to_string(message_count) + " 条消息 (400Hz)");
            }
        }
        
        // 频率控制
        auto elapsed = std::chrono::high_resolution_clock::now() - loop_start;
        if (elapsed < interval) {
            std::this_thread::sleep_for(interval - elapsed);
        }
    }
}

/**
 * @brief 正向链路: 处理/handshake/request/noa消息
 */
void handleForwardMessage(const std::string& topic, const std::string& payload) {
    try {
        Json::Value json_data;
        Json::Reader reader;
        
        if (reader.parse(payload, json_data)) {
            std::lock_guard<std::mutex> lock(g_forward_mutex);
            
            if (json_data.isMember("noa_active")) {
                g_handshake_state["noa_active_request"] = json_data["noa_active"].asDouble();
                g_logger->log("INFO", "正向链路状态更新: noa_active_request=" + 
                             std::to_string(g_handshake_state["noa_active_request"]));
            }
        }
    } catch (const std::exception& e) {
        g_logger->log("ERROR", "正向链路消息解析失败: " + std::string(e.what()));
    }
}

// =========================== 反向链路逻辑 ===========================

/**
 * @brief 反向链路: 处理本地MQTT消息并转发
 */
void handleReverseMessage(const std::string& topic, const std::string& payload) {
    // 直接转发到外部服务器
    bool success = publishToExternal(topic, payload);
    
    if (success) {
        g_logger->log("INFO", "反向链路转发成功: " + topic);
    } else {
        g_logger->log("ERROR", "反向链路转发失败: " + topic);
    }
}

// =========================== MQTT回调函数 ===========================

void on_connect_external(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        std::cout << "[SUCCESS] 外部MQTT连接成功: " << EXTERNAL_HOST << ":" << EXTERNAL_PORT << std::endl;
        
        // 订阅正向链路话题
        mosquitto_subscribe(mosq, nullptr, "/handshake/request/noa", 0);
        g_logger->log("INFO", "订阅正向链路话题: /handshake/request/noa");
    } else {
        std::cout << "[ERROR] 外部MQTT连接失败" << std::endl;
    }
}

void on_connect_local(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        std::cout << "[SUCCESS] 本地MQTT连接成功: " << LOCAL_HOST << ":" << LOCAL_PORT << std::endl;
        
        // 订阅反向链路话题 (带VID)
        std::vector<std::string> reverse_topics = {
            "/handshake/request/" + g_vehicle_id,
            "/handshake/response/" + g_vehicle_id,
            "/vehicle/vehicle_status/" + g_vehicle_id,
            "/vehicle/control_cmd/" + g_vehicle_id
        };
        
        for (const auto& topic : reverse_topics) {
            mosquitto_subscribe(mosq, nullptr, topic.c_str(), 0);
            g_logger->log("INFO", "订阅反向链路话题: " + topic);
        }
    } else {
        std::cout << "[ERROR] 本地MQTT连接失败" << std::endl;
    }
}

void on_message_external(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    std::string topic = message->topic;
    std::string payload(static_cast<char*>(message->payload), message->payloadlen);
    
    if (topic == "/handshake/request/noa") {
        handleForwardMessage(topic, payload);
    }
}

void on_message_local(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    std::string topic = message->topic;
    std::string payload(static_cast<char*>(message->payload), message->payloadlen);
    
    // 检查是否是反向链路话题
    if (topic.find("/" + g_vehicle_id) != std::string::npos) {
        handleReverseMessage(topic, payload);
    }
}

// =========================== 初始化和清理 ===========================

bool initMQTT() {
    mosquitto_lib_init();
    
    // 初始化外部MQTT客户端
    external_mosq = mosquitto_new("bridge_forwarder_external", true, nullptr);
    if (!external_mosq) {
        std::cout << "[ERROR] 创建外部MQTT客户端失败" << std::endl;
        return false;
    }
    
    mosquitto_connect_callback_set(external_mosq, on_connect_external);
    mosquitto_message_callback_set(external_mosq, on_message_external);
    
    // 初始化本地MQTT客户端
    local_mosq = mosquitto_new("bridge_forwarder_local", true, nullptr);
    if (!local_mosq) {
        std::cout << "[ERROR] 创建本地MQTT客户端失败" << std::endl;
        return false;
    }
    
    mosquitto_connect_callback_set(local_mosq, on_connect_local);
    mosquitto_message_callback_set(local_mosq, on_message_local);
    
    // 连接到服务器
    if (mosquitto_connect(external_mosq, EXTERNAL_HOST.c_str(), EXTERNAL_PORT, 60) != MOSQ_ERR_SUCCESS) {
        std::cout << "[ERROR] 连接外部MQTT失败" << std::endl;
        return false;
    }
    
    if (mosquitto_connect(local_mosq, LOCAL_HOST.c_str(), LOCAL_PORT, 60) != MOSQ_ERR_SUCCESS) {
        std::cout << "[ERROR] 连接本地MQTT失败" << std::endl;
        return false;
    }
    
    // 启动网络循环
    mosquitto_loop_start(external_mosq);
    mosquitto_loop_start(local_mosq);
    
    return true;
}

void cleanup() {
    running = false;
    
    if (external_mosq) {
        mosquitto_loop_stop(external_mosq, true);
        mosquitto_destroy(external_mosq);
    }
    
    if (local_mosq) {
        mosquitto_loop_stop(local_mosq, true);
        mosquitto_destroy(local_mosq);
    }
    
    mosquitto_lib_cleanup();
    std::cout << "[SUCCESS] 清理完成" << std::endl;
}

void signal_handler(int sig) {
    std::cout << "\n接收到停止信号，准备退出..." << std::endl;
    cleanup();
    exit(0);
}

// =========================== 主函数 ===========================

int main() {
    std::cout << "双向桥接转发器" << std::endl;
    std::cout << "==============" << std::endl;
    std::cout << "正向链路: /handshake/request/noa → 400Hz聚合发送" << std::endl;
    std::cout << "反向链路: 本地MQTT带VID话题 → 外部服务器" << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 读取VID配置
    g_vehicle_id = readVIDFromConfig();
    
    // 初始化日志
    g_logger = std::make_unique<AsyncLogger>("bridge_forwarder");
    std::cout << "日志文件在 logs/bridge_forwarder/" << std::endl;
    
    // 初始化MQTT
    if (!initMQTT()) {
        cleanup();
        return 1;
    }
    
    // 启动正向链路线程
    std::thread forward_thread(forwardLinkThread);
    
    std::cout << "[SUCCESS] 双向桥接转发器启动成功" << std::endl;
    std::cout << "正向: 400Hz发送4个IDL到外部服务器" << std::endl;
    std::cout << "反向: 转发VID=" << g_vehicle_id << "的消息到外部服务器" << std::endl;
    
    // 主循环
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    forward_thread.join();
    cleanup();
    return 0;
}
