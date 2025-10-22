/**
 * @file vehicle_instruction_takeover.cpp
 * @brief 云舱接管功能处理器
 * 
 * 功能:
 * - 订阅MQTT topic: /vehicle/instruction/takeover/<vid>
 * - 接收云端指令并立即回复已接收确认消息
 * - 执行NOA启动和刹车停止命令
 * - 完成后回复已处理状态消息
 * 
 * 使用方式：
 * ./vehicle_instruction_takeover
 */

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <signal.h>
#include <cstdlib>
#include <yaml-cpp/yaml.h>
#include <jsoncpp/json/json.h>
#include <mosquitto.h>
#include <sstream>
#include "logger_conf/universal_logger.h"

// 全局变量
std::atomic<bool> g_running{true};
std::string g_vid;
std::string g_subscribe_topic;
std::string g_response_topic;
struct mosquitto* g_mosq = nullptr;

/**
 * @brief 信号处理函数
 */
void signal_handler(int signal) {
    LOG_INFO("接收到停止信号，准备退出...");
    g_running = false;
}

/**
 * @brief 从vehicle_config.yaml读取车辆ID
 */
std::string readVIDFromConfig() {
    try {
        YAML::Node config = YAML::LoadFile("../car_config/vehicle_config.yaml");
        if (config["vehicle"] && config["vehicle"]["vid"]) {
            return config["vehicle"]["vid"].as<std::string>();
        }
    } catch (...) {
        // 配置文件读取失败，使用默认值
    }
    return "10001001";
}

/**
 * @brief 获取当前时间戳(毫秒)
 */
int64_t getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

/**
 * @brief 发送MQTT消息
 */
bool sendMQTTMessage(const std::string& topic, const std::string& message) {
    if (!g_mosq) return false;
    
    int ret = mosquitto_publish(g_mosq, nullptr, topic.c_str(), message.length(), message.c_str(), 0, false);
    if (ret == MOSQ_ERR_SUCCESS) {
        LOG_INFO(std::string("发送: ") + message);
        return true;
    }
    return false;
}

/**
 * @brief 创建响应消息
 */
std::string createResponse(const std::string& action, int64_t counter, int64_t timestamp, 
                          int64_t mqtt_timestamp, const std::string& error = "") {
    std::ostringstream oss;
    oss << "{\"action\":" << action << ",\"counter\":" << counter 
        << ",\"timestamp\":" << timestamp 
        << ",\"mqtt_" << action << "_timestamp\":" << mqtt_timestamp;
    
    if (!error.empty()) {
        oss << ",\"error\":\"" << error << "\"";
    }
    
    oss << "}";
    return oss.str();
}

/**
 * @brief 执行NOA启动和刹车停止命令
 */
bool executeNOACommand(int64_t vehicle_instruction_takeover_timestamp) {
    LOG_INFO("执行NOA命令...");
    std::cout << "[调用前时间戳] " << getCurrentTimestamp() << " ms - 执行命令: " "发送NOA激活请求" << std::endl;
    // 发送NOA激活请求
    std::string cmd1 = "./mqtt_sender handshake_request noa_active_request=1 vehicle_instruction_takeover_timestamp=" + std::to_string(vehicle_instruction_takeover_timestamp);
    
    int result1 = std::system(cmd1.c_str());
    std::cout << "[完成后时间戳] " << getCurrentTimestamp() << " ms - NOA激活请求已发送 (返回值: " << result1 << ")" << std::endl;
    if (result1 != 0) {
        LOG_ERROR("发送NOA激活请求失败");
        return false;
    }
    std::cout << "[调用前时间戳] " << getCurrentTimestamp() << " ms - 执行命令: "  "发送加减速-1" << std::endl;
    // 发送加减速-1
    std::string cmd2 = "./mqtt_sender control_cmd target_acceleration_enable=1 target_acceleration=-1 vehicle_instruction_takeover_timestamp=" + std::to_string(vehicle_instruction_takeover_timestamp);
    
    int result2 = std::system(cmd2.c_str());
    std::cout << "[完成后时间戳] " << getCurrentTimestamp() << " ms - 加减速-1已发送 (返回值: " << result2 << ")" << std::endl;
    if (result2 != 0) {
        LOG_ERROR("发送加减速-1失败");
        return false;
    }
    LOG_INFO("已发送加减速-1，等待1秒...");
    
    // 等待1秒
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "[调用前时间戳] " << getCurrentTimestamp() << " ms - 执行命令: " "发送加减速-2" << std::endl;
    // 再发送加减速-2
    std::string cmd3 = "./mqtt_sender control_cmd target_acceleration_enable=1 target_acceleration=-1.5 vehicle_instruction_takeover_timestamp=" + std::to_string(vehicle_instruction_takeover_timestamp);
    
    int result3 = std::system(cmd3.c_str());
    std::cout << "[完成后时间戳] " << getCurrentTimestamp() << " ms - 加减速-2已发送 (返回值: " << result3 << ")" << std::endl;
    if (result3 != 0) {
        LOG_ERROR("发送加减速-2失败");
        return false;
    }
    LOG_INFO("已发送加减速-2");
    
    return true;
}

/**
 * @brief 处理接管消息
 */
void processTakeoverMessage(const std::string& message) {
    Json::Value root;
    Json::Reader reader;
    
    if (!reader.parse(message, root) || !root.isMember("counter") || !root.isMember("timestamp")) {
        LOG_ERROR("消息格式错误");
        return;
    }
    
    int64_t counter = root["counter"].asInt64();
    int64_t timestamp = root["timestamp"].asInt64();
    
    // 1. 发送"received"响应
    std::string received_response = createResponse("received", counter, timestamp, getCurrentTimestamp());
    if (!sendMQTTMessage(g_response_topic, received_response)) {
        return;
    }
    
    // 2. 执行NOA命令，传递原始timestamp作为debug_timestamp
    bool success = executeNOACommand(timestamp);
    
    // 3. 发送"processed"响应
    std::string error = success ? "" : "命令执行失败";
    std::string processed_response = createResponse("processed", counter, timestamp, getCurrentTimestamp(), error);
    sendMQTTMessage(g_response_topic, processed_response);
    
    LOG_INFO("处理完成");
}

/**
 * @brief MQTT消息回调函数
 */
void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    if (!message || !message->payload) return;
    
    std::string topic(message->topic);
    std::string payload(static_cast<char*>(message->payload), message->payloadlen);
    
    if (topic == g_subscribe_topic) {
        LOG_INFO(std::string("收到消息 [") + topic + "] " + payload);
        processTakeoverMessage(payload);
    }
}

/**
 * @brief MQTT连接回调函数
 */
void on_connect(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        mosquitto_subscribe(mosq, nullptr, g_subscribe_topic.c_str(), 0);
        LOG_INFO("MQTT连接成功，等待指令...");
    } else {
        LOG_ERROR("MQTT连接失败");
        g_running = false;
    }
}

/**
 * @brief 初始化MQTT客户端
 */
bool initMQTT() {
    mosquitto_lib_init();
    
    g_mosq = mosquitto_new(("vehicle_instruction_takeover_8975" + g_vid).c_str(), true, nullptr);
    if (!g_mosq) return false;
    
    mosquitto_connect_callback_set(g_mosq, on_connect);
    mosquitto_message_callback_set(g_mosq, on_message);
    
    if (mosquitto_connect(g_mosq, "hellorobotaxi.cn", 11883, 60) != MOSQ_ERR_SUCCESS) {
        return false;
    }
    
    return mosquitto_loop_start(g_mosq) == MOSQ_ERR_SUCCESS;
}

/**
 * @brief 清理MQTT资源
 */
void cleanupMQTT() {
    if (g_mosq) {
        mosquitto_loop_stop(g_mosq, true);
        mosquitto_destroy(g_mosq);
        g_mosq = nullptr;
    }
    mosquitto_lib_cleanup();
}

int main() {
    std::cout << "云舱接管功能处理器" << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    initUniversalLogger("vehicle_instruction_takeover");

    g_vid = readVIDFromConfig();
    g_subscribe_topic = "/vehicle/instruction/takeover/" + g_vid;
    g_response_topic = "/vehicle/instruction/takeover/" + g_vid + "/response";
    
    LOG_INFO(std::string("VID: ") + g_vid);

    if (!initMQTT()) {
        LOG_ERROR("MQTT初始化失败");
        cleanupUniversalLogger();
        return 1;
    }

    // 持续运行，直到收到退出信号
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    cleanupMQTT();
    LOG_INFO("程序已退出");
    cleanupUniversalLogger();
    
    return 0;
}