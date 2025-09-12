/**
 * @file tsp_forwarder.cpp
 * @brief MQTT程序1: TSP命令转发器
 * 
 * 订阅: tsp/command/{vid}
 * 转发: cloud/noa/{vid}
 * 功能: 透传智驾云的TSP命令并添加MQTT发送时间戳
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

// =========================== 全局变量 ===========================
static struct mosquitto* mosq_client = nullptr;
static bool running = true;

// 全局日志器实例
std::unique_ptr<AsyncLogger> g_logger;

// 服务器配置
const std::string MQTT_HOST = "hellorobotaxi.cn";
const int MQTT_PORT = 11883;

// =========================== 时间戳工具 ===========================
uint64_t getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// =========================== JSON工具 ===========================
std::string addMQTTTimestamp(const std::string& original_json) {
    try {
        Json::Reader reader;
        Json::Value root;
        
        if (!reader.parse(original_json, root)) {
            LOG_ERROR("JSON解析失败: " + original_json);
            return original_json;
        }
        
        // 添加MQTT发送时间戳
        root["mqtt_send_timestamp"] = getCurrentTimestamp();
        
        Json::FastWriter writer;
        return writer.write(root);
        
    } catch (const std::exception& e) {
        LOG_ERROR("添加MQTT时间戳时出错: " + std::string(e.what()));
        return original_json;
    }
}

// =========================== MQTT回调函数 ===========================
void on_connect(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        LOG_INFO("TSP转发器连接MQTT服务器成功: " + MQTT_HOST + ":" + std::to_string(MQTT_PORT));
        
        // 订阅TSP命令主题
        int mid;
        int rc = mosquitto_subscribe(mosq, &mid, "tsp/command/+", 0);
        if (rc == MOSQ_ERR_SUCCESS) {
            LOG_INFO("订阅TSP命令主题: tsp/command/+");
        } else {
            LOG_ERROR("订阅失败: " + std::string(mosquitto_strerror(rc)));
        }
    } else {
        LOG_ERROR("TSP转发器连接失败: " + std::string(mosquitto_connack_string(result)));
    }
}

void on_subscribe(struct mosquitto* mosq, void* userdata, int mid, int qos_count, const int* granted_qos) {
    LOG_INFO("TSP转发器订阅确认 (mid: " + std::to_string(mid) + ")");
}

void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    if (!message->payload) return;
    
    std::string topic = message->topic;
    std::string payload = (char*)message->payload;
    
    LOG_INFO("接收到消息 [" + topic + "]: " + payload);
    
    // 提取VID (tsp/command/123 -> 123)
    size_t last_slash = topic.find_last_of('/');
    if (last_slash == std::string::npos) {
        LOG_ERROR("无效的TSP主题格式: " + topic);
        return;
    }
    
    std::string vid = topic.substr(last_slash + 1);
    std::string forward_topic = "cloud/noa/" + vid;
    
    // 添加MQTT发送时间戳
    std::string enhanced_payload = addMQTTTimestamp(payload);
    
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
        LOG_INFO("TSP转发器正常断开连接");
    } else {
        LOG_ERROR("TSP转发器意外断开: " + std::string(mosquitto_strerror(reason)));
    }
}

// =========================== 信号处理 ===========================
void signal_handler(int signum) {
    LOG_WARN("接收到停止信号(" + std::to_string(signum) + ")，TSP转发器准备退出...");
    running = false;
    
    if (mosq_client) {
        mosquitto_disconnect(mosq_client);
    }
}

// =========================== 主函数 ===========================
int main() {
    // 初始化异步日志器
    g_logger = std::make_unique<AsyncLogger>("tsp_forwarder");
    
    LOG_INFO("=== TSP命令转发器 (MQTT程序1) ===");
    LOG_INFO("订阅: tsp/command/+ → 转发: cloud/noa/{vid}");
    LOG_INFO("服务器: " + MQTT_HOST + ":" + std::to_string(MQTT_PORT));
    LOG_INFO("日志文件: " + g_logger->get_log_path());
    
    // 注册信号处理器
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化mosquitto库
    mosquitto_lib_init();
    LOG_INFO("Mosquitto库初始化完成");
    
    // 创建mosquitto实例
    mosq_client = mosquitto_new("tsp_forwarder", true, nullptr);
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
    
    LOG_INFO("TSP转发器启动成功，等待TSP命令...");
    std::cout << "启动了TSP转发器" << std::endl;
    std::cout << "日志文件在 " << g_logger->get_log_path() << std::endl;
    
    // 主循环
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 清理资源
    LOG_INFO("开始清理TSP转发器资源...");
    mosquitto_loop_stop(mosq_client, false);
    mosquitto_destroy(mosq_client);
    mosquitto_lib_cleanup();
    
    LOG_INFO("TSP转发器退出完成");
    
    // 停止异步日志器
    if (g_logger) {
        g_logger->stop();
        g_logger.reset();
    }
    
    return 0;
}
