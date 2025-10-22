/**
 * @file vehicle_instruction_braking.cpp
 * @brief 车辆刹车指令处理器
 * 
 * 功能:
 * - 订阅MQTT topic: /vehicle/instruction/braking/<vid>
 * - 接收云端指令并立即回复已接收确认消息
 * - 执行刹车命令
 * - 完成后回复已处理状态消息
 * 
 * 使用方式：
 * ./vehicle_instruction_braking
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
    static int signal_count = 0;
    signal_count++;
    
    if (signal_count == 1) {
        std::cout << "\n接收到停止信号，准备退出..." << std::endl;
        g_running = false;
    } else {
        std::cout << "\n强制退出..." << std::endl;
        std::_Exit(1);
    }
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
        std::cout << "[INFO] 发送响应: " << message << std::endl;
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
 * @brief 执行刹车命令
 */
bool executeBrakingCommand() {
    // 先发送加减速-1
    int result1 = std::system("./mqtt_sender control_cmd target_acceleration_enable=1 target_acceleration=-1");
    if (result1 != 0) {
        std::cerr << "[ERROR] 发送加减速-1失败" << std::endl;
        return false;
    }
    std::cout << "[INFO] 已发送加减速-1，等待1秒..." << std::endl;
    
    // 等待1秒
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 再发送加减速-2
    int result2 = std::system("./mqtt_sender control_cmd target_acceleration_enable=1 target_acceleration=-1.5");
    if (result2 != 0) {
        std::cerr << "[ERROR] 发送加减速-2失败" << std::endl;
        return false;
    }
    std::cout << "[INFO] 已发送加减速-2" << std::endl;
    
    return true;
}

/**
 * @brief 处理刹车消息
 */
void processBrakingMessage(const std::string& message) {
    Json::Value root;
    Json::Reader reader;
    
    if (!reader.parse(message, root) || !root.isMember("counter") || !root.isMember("timestamp")) {
        std::cerr << "[ERROR] 消息格式错误" << std::endl;
        return;
    }
    
    int64_t counter = root["counter"].asInt64();
    int64_t timestamp = root["timestamp"].asInt64();
    
    // 1. 发送"received"响应
    std::string received_response = createResponse("received", counter, timestamp, getCurrentTimestamp());
    if (!sendMQTTMessage(g_response_topic, received_response)) {
        return;
    }
    
    // 2. 执行刹车命令
    bool success = executeBrakingCommand();
    
    // 3. 发送"processed"响应
    std::string error = success ? "" : "刹车命令执行失败";
    std::string processed_response = createResponse("processed", counter, timestamp, getCurrentTimestamp(), error);
    sendMQTTMessage(g_response_topic, processed_response);
    
    std::cout << "[INFO] 处理完成" << std::endl;
}

/**
 * @brief MQTT消息回调函数
 */
void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    if (!message || !message->payload) return;
    
    std::string topic(message->topic);
    std::string payload(static_cast<char*>(message->payload), message->payloadlen);
    
    if (topic == g_subscribe_topic) {
        std::cout << "[INFO] 收到消息 [" << topic << "] " << payload << std::endl;
        processBrakingMessage(payload);
    }
}

/**
 * @brief MQTT连接回调函数
 */
void on_connect(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        std::cout << "[INFO] MQTT连接成功" << std::endl;
        mosquitto_subscribe(mosq, nullptr, g_subscribe_topic.c_str(), 0);
    } else {
        std::cerr << "[ERROR] MQTT连接失败" << std::endl;
    }
}

/**
 * @brief MQTT断连回调函数
 */
void on_disconnect(struct mosquitto* mosq, void* userdata, int result) {
    if (result != 0) {
        std::cerr << "[WARNING] MQTT意外断开连接" << std::endl;
        // 单线程模式下，由主循环处理重连
    }
}

/**
 * @brief 初始化MQTT客户端
 */
bool initMQTT() {
    mosquitto_lib_init();
    
    std::string client_id = "vehicle_instruction_braking_8975" + g_vid;
    g_mosq = mosquitto_new(client_id.c_str(), true, nullptr);
    if (!g_mosq) return false;
    
    mosquitto_connect_callback_set(g_mosq, on_connect);
    mosquitto_message_callback_set(g_mosq, on_message);
    mosquitto_disconnect_callback_set(g_mosq, on_disconnect);
    
    if (mosquitto_connect(g_mosq, "hellorobotaxi.cn", 11883, 60) != MOSQ_ERR_SUCCESS) {
        return false;
    }
    
    return true;
}

/**
 * @brief 清理MQTT资源
 */
void cleanupMQTT() {
    if (g_mosq) {
        mosquitto_disconnect(g_mosq);
        mosquitto_destroy(g_mosq);
        g_mosq = nullptr;
    }
    mosquitto_lib_cleanup();
}

int main() {
    std::cout << "车辆刹车指令处理器" << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    g_vid = readVIDFromConfig();
    g_subscribe_topic = "/vehicle/instruction/braking/" + g_vid;
    g_response_topic = "/vehicle/instruction/braking/" + g_vid + "/response";
    
    std::cout << "VID: " << g_vid << std::endl;

    if (!initMQTT()) {
        std::cerr << "[ERROR] MQTT初始化失败" << std::endl;
        return 1;
    }

    std::cout << "开始监听刹车指令..." << std::endl;

    // 单线程事件循环，使用1ms超时以最小化延时
    while (g_running) {
        int rc = mosquitto_loop(g_mosq, 1, 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            std::cerr << "[ERROR] MQTT循环错误，尝试重连..." << std::endl;
            mosquitto_reconnect(g_mosq);
        }
    }

    cleanupMQTT();
    std::cout << "程序已退出" << std::endl;
    return 0;
}