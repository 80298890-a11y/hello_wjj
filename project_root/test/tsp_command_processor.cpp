/**
 * @file tsp_command_processor.cpp
 * @brief TSP命令处理器: 监听云端tsp/command/{vid}，转发到/handshake/request
 */

#include <iostream>
#include <mosquitto.h>
#include <jsoncpp/json/json.h>
#include <chrono>
#include <thread>
#include <string>
#include <map>
#include <signal.h>
#include <memory>
#include <atomic>
#include <fstream>
#include "logger_conf/universal_logger.h"

// 全局变量
static struct mosquitto* mosq_client = nullptr;
static std::atomic<bool> running{true};
static std::string g_vehicle_id = "12345678"; // 默认VID，从配置文件读取

// 服务器配置
const std::string MQTT_HOST = "hellorobotaxi.cn";
const int MQTT_PORT = 11883;

// 时间戳工具
uint64_t getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// VID配置读取
std::string readVIDFromConfig() {
    std::ifstream file("../car_config/vehicle_config.yaml");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            size_t pos = line.find("vid:");
            if (pos != std::string::npos) {
                size_t start = line.find('"', pos);
                size_t end = line.find('"', start + 1);
                if (start != std::string::npos && end != std::string::npos) {
                    return line.substr(start + 1, end - start - 1);
                }
            }
        }
    }
    return "10001001";
}

// 发送三帧到handshake_request
void sendHandshakeRequest(long noa_active_request) {
    std::string cmd = "./mqtt_sender handshake_request noa_active_request=" + std::to_string(noa_active_request);
    std::cout << "🚀 " << cmd << std::endl;
    std::system(cmd.c_str());
}


// TSP命令处理
void processTSPCommand(const std::string& payload, const std::string& vid) {
    Json::Reader reader;
    Json::Value root;
    
    if (!reader.parse(payload, root) || !root.isMember("action") || !root.isMember("module")) {
        return;
    }
    
    std::string action = root["action"].asString();
    std::string module = root["module"].asString();
    
    if (module != "noa") return;
    
    long noa_value = -999;
    if (action == "start") noa_value = 1;
    else if (action == "hold") noa_value = 0;
    else if (action == "stop") noa_value = -1;
    else return;
    
    std::cout << "✅ " << action << " → noa_active_request=" << noa_value << std::endl;
    sendHandshakeRequest(noa_value);
}

// MQTT回调
void on_connect(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        mosquitto_subscribe(mosq, nullptr, "tsp/command/10001001", 0);
        std::cout << "✅ 已订阅 tsp/command/10001001" << std::endl;
    }
}

void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    if (!message->payload) return;
    
    std::string payload = (char*)message->payload;
    std::cout << "📨 " << payload << std::endl;
    
    size_t last_slash = std::string(message->topic).find_last_of('/');
    std::string vid = last_slash != std::string::npos ? 
                      std::string(message->topic).substr(last_slash + 1) : "10001001";
    
    processTSPCommand(payload, vid);
}

void signal_handler(int signum) {
    running = false;
}

int main() {
    initUniversalLogger("tsp_command_processor");
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    g_vehicle_id = readVIDFromConfig();
    std::cout << "TSP命令处理器 [VID: " << g_vehicle_id << "]" << std::endl;
    
    mosquitto_lib_init();
    mosq_client = mosquitto_new("tsp_cmd_processor", true, nullptr);
    if (!mosq_client) return 1;
    
    mosquitto_connect_callback_set(mosq_client, on_connect);
    mosquitto_message_callback_set(mosq_client, on_message);
    
    if (mosquitto_connect(mosq_client, MQTT_HOST.c_str(), MQTT_PORT, 60) != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mosq_client);
        mosquitto_lib_cleanup();
        return 1;
    }
    
    while (running) {
        if (mosquitto_loop(mosq_client, 1, 1) != MOSQ_ERR_SUCCESS) {
            mosquitto_reconnect(mosq_client);
        }
    }
    
    mosquitto_destroy(mosq_client);
    mosquitto_lib_cleanup();
    cleanupUniversalLogger();
    return 0;
}
