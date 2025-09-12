/**
 * @file data_processor.cpp
 * @brief MQTT程序2: 数据处理器
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

// Handshake Request 状态维护
struct HandshakeRequestState {
    long noa_active = 1;        
    long override_status = 0;   
    long override_ready = 1;    
    uint64_t timestamp = 0;     
    
    void updateNoaActive(int new_value) {
        noa_active = new_value;
        timestamp = getCurrentTimestamp();
    }
    
    std::string toJson() const {
        Json::Value json;
        json["noa_active"] = noa_active;
        json["override_status"] = override_status;
        json["override_ready"] = override_ready;
        json["timestamp"] = Json::UInt64(timestamp);
        
        Json::FastWriter writer;
        return writer.write(json);
    }
};

static HandshakeRequestState g_handshake_state;

// 数据处理工具
bool processActionUpdate(const std::string& original_json, const std::string& vid) {
    try {
        Json::Reader reader;
        Json::Value root;
        
        if (!reader.parse(original_json, root)) {
            LOG_ERROR("JSON解析失败: " + original_json);
            return false;
        }
        
        std::string action = "start";
        if (root.isMember("action")) {
            action = root["action"].asString();
        }
        
        int new_noa_active = 1;
        if (action == "stop") {
            new_noa_active = 0;
        } else if (action == "start") {
            new_noa_active = 1;
        }
        
        // 不管状态是否变化，都要更新并发送消息
        if (g_handshake_state.noa_active != new_noa_active) {
            LOG_INFO("VID-" + vid + " 状态变更: " + std::to_string(g_handshake_state.noa_active) + 
                    " → " + std::to_string(new_noa_active) + " (action=" + action + ")");
        } else {
            LOG_INFO("VID-" + vid + " 接收到消息: noa_active=" + std::to_string(new_noa_active) + 
                     " (action=" + action + ") - 状态保持不变但仍会更新");
        }
        
        // 总是更新状态并发送消息
        g_handshake_state.updateNoaActive(new_noa_active);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("处理数据时出错: " + std::string(e.what()));
        return false;
    }
}

// MQTT回调函数
void on_connect(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        LOG_INFO("数据处理器连接服务器成功: " + MQTT_HOST + ":" + std::to_string(MQTT_PORT));
        
        int mid;
        int rc = mosquitto_subscribe(mosq, &mid, "zhan/send/+", 0);
        if (rc == MOSQ_ERR_SUCCESS) {
            LOG_INFO("订阅处理主题: zhan/send/+");
        } else {
            LOG_ERROR("订阅失败: " + std::string(mosquitto_strerror(rc)));
        }
    } else {
        LOG_ERROR("数据处理器连接失败: " + std::string(mosquitto_connack_string(result)));
    }
}

void on_subscribe(struct mosquitto* mosq, void* userdata, int mid, int qos_count, const int* granted_qos) {
    LOG_INFO("数据处理器订阅确认 (mid: " + std::to_string(mid) + ")");
}

void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    if (!message->payload) return;
    
    std::string topic = message->topic;
    std::string payload = (char*)message->payload;
    
    LOG_INFO("接收到消息 [" + topic + "]: " + payload);
    
    // 提取VID
    size_t last_slash = topic.find_last_of('/');
    if (last_slash == std::string::npos) {
        LOG_ERROR("无效的处理主题格式: " + topic);
        return;
    }
    
    std::string vid = topic.substr(last_slash + 1);
    
    // 检查是否需要更新状态
    bool need_update = processActionUpdate(payload, vid);
    if (!need_update) {
        return;
    }
    
    // 状态已更新，发送完整的handshake/request状态到云端
    std::string target_topic = "/handshake/request/noa";
    std::string handshake_json = g_handshake_state.toJson();
    
    int rc = mosquitto_publish(mosq, nullptr, target_topic.c_str(), 
                              handshake_json.length(), handshake_json.c_str(), 0, false);
    
    if (rc == MOSQ_ERR_SUCCESS) {
        LOG_INFO("状态更新发送成功 [VID-" + vid + " → " + target_topic + "]");
        LOG_DEBUG("完整handshake状态: " + handshake_json);
    } else {
        LOG_ERROR("发送失败: " + std::string(mosquitto_strerror(rc)));
    }
}

void on_disconnect(struct mosquitto* mosq, void* userdata, int reason) {
    if (reason == 0) {
        LOG_INFO("数据处理器正常断开连接");
    } else {
        LOG_ERROR("数据处理器意外断开: " + std::string(mosquitto_strerror(reason)));
    }
}

// 信号处理
void signal_handler(int signum) {
    LOG_WARN("接收到停止信号(" + std::to_string(signum) + ")，数据处理器准备退出...");
    running = false;
    
    if (mosq_client) {
        mosquitto_disconnect(mosq_client);
    }
}

// 主函数
int main() {
    // 初始化异步日志器
    g_logger = std::make_unique<AsyncLogger>("data_processor");
    
    std::cout << "启动了数据处理器" << std::endl;
    std::cout << "日志文件在 " << g_logger->get_log_path() << std::endl;
    
    LOG_INFO("=== 数据处理器 (MQTT程序2) ===");
    LOG_INFO("订阅: zhan/send/+ → 维护状态 → 发送: /handshake/request/noa");
    LOG_INFO("服务器: " + MQTT_HOST + ":" + std::to_string(MQTT_PORT));
    
    // 注册信号处理器
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化mosquitto库
    mosquitto_lib_init();
    LOG_INFO("Mosquitto库初始化完成");
    
    // 创建mosquitto实例
    mosq_client = mosquitto_new("data_processor", true, nullptr);
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
    
    LOG_INFO("数据处理器启动成功，等待处理消息...");
    
    // 主循环
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 清理资源
    LOG_INFO("开始清理数据处理器资源...");
    mosquitto_loop_stop(mosq_client, false);
    mosquitto_destroy(mosq_client);
    mosquitto_lib_cleanup();
    
    LOG_INFO("数据处理器退出完成");
    
    // 停止异步日志器
    if (g_logger) {
        g_logger->stop();
        g_logger.reset();
    }
    
    return 0;
}