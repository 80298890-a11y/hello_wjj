/**
 * @file mqtt_simulator.cpp
 * @brief MQTT程序X: MQTT模拟中转程序
 */

#include <iostream>
#include <mosquitto.h>
#include <json/json.h>
#include <chrono>
#include <thread>
#include <string>
#include <map>
#include <signal.h>
#include <memory>
#include "async_logger.hpp"

// 全局变量
static struct mosquitto* mosq_client = nullptr;
static bool running = true;
std::unique_ptr<AsyncLogger> g_logger;

// 服务器配置
const std::string MQTT_HOST = "hellorobotaxi.cn";
const int MQTT_PORT = 11883;

// 时间戳工具
uint64_t getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// JSON工具
std::string addSimulatorTimestamp(const std::string& original_json) {
    try {
        Json::Reader reader;
        Json::Value root;
        
        if (!reader.parse(original_json, root)) {
            LOG_ERROR("JSON解析失败: " + original_json);
            return original_json;
        }
        
        root["simulator_timestamp"] = getCurrentTimestamp();
        
        Json::FastWriter writer;
        return writer.write(root);
        
    } catch (const std::exception& e) {
        LOG_ERROR("添加模拟器时间戳时出错: " + std::string(e.what()));
        return original_json;
    }
}

// MQTT回调函数
void on_connect(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        LOG_INFO("MQTT模拟器连接服务器成功: " + MQTT_HOST + ":" + std::to_string(MQTT_PORT));
        
        int mid;
        int rc = mosquitto_subscribe(mosq, &mid, "cloud/noa/+", 0);
        if (rc == MOSQ_ERR_SUCCESS) {
            LOG_INFO("订阅云端任务主题: cloud/noa/+");
        } else {
            LOG_ERROR("订阅失败: " + std::string(mosquitto_strerror(rc)));
        }
    } else {
        LOG_ERROR("MQTT模拟器连接失败: " + std::string(mosquitto_connack_string(result)));
    }
}

void on_subscribe(struct mosquitto* mosq, void* userdata, int mid, int qos_count, const int* granted_qos) {
    LOG_INFO("MQTT模拟器订阅确认 (mid: " + std::to_string(mid) + ")");
}

void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    if (!message->payload) return;
    
    std::string topic = message->topic;
    std::string payload = (char*)message->payload;
    
    LOG_INFO("接收到云端消息 [" + topic + "]: " + payload);
    
    // 提取VID
    size_t last_slash = topic.find_last_of('/');
    if (last_slash == std::string::npos) {
        LOG_ERROR("无效的云端主题格式: " + topic);
        return;
    }
    
    std::string vid = topic.substr(last_slash + 1);
    std::string forward_topic = "zhan/send/" + vid;
    
    // 添加模拟器时间戳
    std::string enhanced_payload = addSimulatorTimestamp(payload);
    
    // 转发到新主题
    int rc = mosquitto_publish(mosq, nullptr, forward_topic.c_str(), 
                              enhanced_payload.length(), enhanced_payload.c_str(), 0, false);
    
    if (rc == MOSQ_ERR_SUCCESS) {
        LOG_INFO("转发成功 [" + topic + " → " + forward_topic + "]");
        LOG_DEBUG("增强后的消息: " + enhanced_payload);
    } else {
        LOG_ERROR("转发失败: " + std::string(mosquitto_strerror(rc)));
    }
}

void on_disconnect(struct mosquitto* mosq, void* userdata, int reason) {
    if (reason == 0) {
        LOG_INFO("MQTT模拟器正常断开连接");
    } else {
        LOG_ERROR("MQTT模拟器意外断开: " + std::string(mosquitto_strerror(reason)));
    }
}

// 信号处理
void signal_handler(int signum) {
    LOG_WARN("接收到停止信号(" + std::to_string(signum) + ")，MQTT模拟器准备退出...");
    running = false;
    
    if (mosq_client) {
        mosquitto_disconnect(mosq_client);
    }
}

// 主函数
int main() {
    // 初始化异步日志器
    g_logger = std::make_unique<AsyncLogger>("mqtt_simulator");
    
    std::cout << "启动了MQTT模拟器" << std::endl;
    std::cout << "日志文件在 " << g_logger->get_log_path() << std::endl;
    
    LOG_INFO("=== MQTT模拟器 (MQTT程序X) ===");
    LOG_INFO("订阅: cloud/noa/+ → 转发: zhan/send/{vid}");
    LOG_INFO("服务器: " + MQTT_HOST + ":" + std::to_string(MQTT_PORT));
    
    // 注册信号处理器
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化mosquitto库
    mosquitto_lib_init();
    LOG_INFO("Mosquitto库初始化完成");
    
    // 创建mosquitto实例
    mosq_client = mosquitto_new("mqtt_simulator", true, nullptr);
    if (!mosq_client) {
        LOG_ERROR("创建MQTT客户端失败");
        mosquitto_lib_cleanup();
        return 1;
    }
    
    // 设置回调函数
    mosquitto_connect_callback_set(mosq_client, on_connect);
    mosquitto_subscribe_callback_set(mosq_client, on_subscribe);
    mosquitto_message_callback_set(mosq_client, on_message);
    mosquitto_disconnect_callback_set(mosq_client, on_disconnect);
    
    // 连接到MQTT服务器
    int rc = mosquitto_connect(mosq_client, MQTT_HOST.c_str(), MQTT_PORT, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR("连接MQTT服务器失败: " + std::string(mosquitto_strerror(rc)));
        mosquitto_destroy(mosq_client);
        mosquitto_lib_cleanup();
        return 1;
    }
    
    // 启动网络循环
    rc = mosquitto_loop_start(mosq_client);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR("启动网络循环失败: " + std::string(mosquitto_strerror(rc)));
        mosquitto_destroy(mosq_client);
        mosquitto_lib_cleanup();
        return 1;
    }
    
    LOG_INFO("MQTT模拟器启动成功，等待云端消息...");
    
    // 主循环
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 清理资源
    LOG_INFO("开始清理MQTT模拟器资源...");
    mosquitto_loop_stop(mosq_client, false);
    mosquitto_destroy(mosq_client);
    mosquitto_lib_cleanup();
    
    LOG_INFO("MQTT模拟器退出完成");
    
    // 停止异步日志器
    if (g_logger) {
        g_logger->stop();
        g_logger.reset();
    }
    
    return 0;
}