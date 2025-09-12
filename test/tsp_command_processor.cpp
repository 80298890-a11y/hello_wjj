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
static std::atomic<bool> should_send{false};
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

// VID配置读取函数
std::string readVIDFromConfig() {
    std::ifstream file("../car_config/vehicle_config.yaml");
    if (!file.is_open()) {
        LOG_WARN("无法打开vehicle_config.yaml文件，使用默认VID: 12345678");
        return "12345678";
    }
    
    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find("vid:");
        if (pos != std::string::npos) {
            size_t quote_start = line.find('"', pos);
            if (quote_start != std::string::npos) {
                size_t quote_end = line.find('"', quote_start + 1);
                if (quote_end != std::string::npos) {
                    std::string vid = line.substr(quote_start + 1, quote_end - quote_start - 1);
                    LOG_INFO("从配置文件读取VID: " + vid);
                    return vid;
                }
            }
        }
    }
    
    LOG_WARN("配置文件中未找到vid字段，使用默认VID: 12345678");
    return "12345678";
}

// Handshake Request 状态维护
struct HandshakeRequestState {
    long noa_active_request = 1;        
    uint64_t timestamp = 0;     
    
    // 上一次的状态值（用于检测变化）
    long last_noa_active_request = -999;
    
    void setActiveRequest(const std::string& action = "start") {
        if (action == "start") {
            noa_active_request = 1;     // 启动NOA
            timestamp = getCurrentTimestamp();
        } else if (action == "hold") {
            noa_active_request = 0;     // 暂停NOA
            timestamp = getCurrentTimestamp();
        } else if (action == "stop") {
            noa_active_request = -1;    // 停止NOA
            timestamp = getCurrentTimestamp();
        }
    }
    
    // 检测状态是否有变化
    bool hasChanged() {
        return (noa_active_request != last_noa_active_request);
    }
    
    // 更新lastvalue记录
    void updateLastValues() {
        last_noa_active_request = noa_active_request;
    }
    
    
    std::string toJson() const {
        Json::Value json;
        json["noa_active_request"] = noa_active_request;
        json["timestamp"] = Json::UInt64(timestamp);
        
        Json::FastWriter writer;
        return writer.write(json);
    }
};

static HandshakeRequestState g_handshake_state;


// TSP命令处理
bool processTSPCommand(const std::string& original_json, const std::string& vid) {
    try {
        Json::Reader reader;
        Json::Value root;
        
        if (!reader.parse(original_json, root)) {
            LOG_ERROR("JSON解析失败: " + original_json);
            return false;
        }
        
        // 检查必要字段
        if (!root.isMember("action") || !root.isMember("module")) {
            LOG_WARN("VID-" + vid + " 缺少必要字段(action/module): " + original_json);
            return false;
        }
        
        std::string action = root["action"].asString();
        std::string module = root["module"].asString();
        std::string request_id = root.isMember("requestId") ? root["requestId"].asString() : "";
        uint64_t tsp_timestamp = root.isMember("timestamp") ? root["timestamp"].asUInt64() : 0;
        
        LOG_INFO("VID-" + vid + " TSP命令: action=" + action + ", module=" + module + 
                 ", requestId=" + request_id + ", timestamp=" + std::to_string(tsp_timestamp));
        
        // 检查触发条件: action="start/hold/stop" AND module="noa"
        if ((action == "start" || action == "hold" || action == "stop") && module == "noa") {
            std::string action_desc;
            if (action == "start") {
                action_desc = "启动";
            } else if (action == "hold") {
                action_desc = "暂停";
            } else if (action == "stop") {
                action_desc = "停止";
            }
            
            LOG_INFO("VID-" + vid + " 触发NOA" + action_desc + "命令 - 转发到/handshake/request");
            g_handshake_state.setActiveRequest(action);
            should_send = true;
            return true;
        } else {
            LOG_INFO("VID-" + vid + " 不符合触发条件(需要action=start/hold/stop,module=noa): " + 
                     action + "/" + module);
            return false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("处理TSP命令时出错: " + std::string(e.what()));
        return false;
    }
}

// MQTT回调函数
void on_connect(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        std::cout << "✅ MQTT连接成功 " << MQTT_HOST << ":" << MQTT_PORT << std::endl;
        LOG_INFO("TSP命令处理器连接服务器成功: " + MQTT_HOST + ":" + std::to_string(MQTT_PORT));
        
        int mid;
        // 订阅TSP命令主题
        int rc = mosquitto_subscribe(mosq, &mid, "tsp/command/10001001", 0);
        if (rc == MOSQ_ERR_SUCCESS) {
            std::cout << "✅ 订阅成功 tsp/command/10001001" << std::endl;
            LOG_INFO("订阅TSP命令主题: tsp/command/10001001");
        } else {
            std::cout << "❌ 订阅TSP命令失败: " << mosquitto_strerror(rc) << std::endl;
            LOG_ERROR("订阅TSP命令失败: " + std::string(mosquitto_strerror(rc)));
        }
        
    } else {
        std::cout << "❌ MQTT连接失败: " << mosquitto_connack_string(result) << std::endl;
        LOG_ERROR("TSP命令处理器连接失败: " + std::string(mosquitto_connack_string(result)));
    }
}

void on_subscribe(struct mosquitto* mosq, void* userdata, int mid, int qos_count, const int* granted_qos) {
    std::cout << "✅ 订阅确认: mid=" << mid << std::endl;
    LOG_INFO("TSP命令处理器订阅确认 (mid: " + std::to_string(mid) + ")");
}

void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    if (!message->payload) return;
    
    std::string topic = message->topic;
    std::string payload = (char*)message->payload;
    
    std::cout << "📨 收到了【" << topic << "】「" << payload << "」" << std::endl;
    LOG_INFO("收到了【" + topic + "】「" + payload + "」");
    
    // 只处理TSP命令 (tsp/command/10001001)/
    // 提取VID
    size_t last_slash = topic.find_last_of('/');
    if (last_slash == std::string::npos) {
        LOG_ERROR("无效的TSP命令主题格式: " + topic);
        return;
    }
    
    std::string vid = topic.substr(last_slash + 1);
    
    // 处理TSP命令并显示转换结果
    if (processTSPCommand(payload, vid)) {
        std::string handshake_json = g_handshake_state.toJson();
        // 移除JSON末尾的换行符
        if (!handshake_json.empty() && handshake_json.back() == '\n') {
            handshake_json.pop_back();
        }
        std::string target_topic_with_vid = "/handshake/request/" + g_vehicle_id;
        std::cout << "✅ 转换成功【" << target_topic_with_vid << "】【" << handshake_json << "】" << std::endl;
        LOG_INFO("转换成功【" + target_topic_with_vid + "】【" + handshake_json + "】");
    }
}

void on_disconnect(struct mosquitto* mosq, void* userdata, int reason) {
    if (reason == 0) {
        LOG_INFO("TSP命令处理器正常断开连接");
    } else {
        LOG_ERROR("TSP命令处理器意外断开: " + std::string(mosquitto_strerror(reason)));
    }
}

// 持续发送线程
void continuous_sender_thread() {
    LOG_INFO("启动10ms持续发送线程 (只在有值变化时发送)");
    
    while (running) {
        // 检查是否有变化或者强制发送
        bool has_changed = g_handshake_state.hasChanged();
        
        if (should_send && has_changed) {
            std::string target_topic = "/handshake/request/" + g_vehicle_id;
            std::string handshake_json = g_handshake_state.toJson();
            
            int rc = mosquitto_publish(mosq_client, nullptr, target_topic.c_str(), 
                                      handshake_json.length(), handshake_json.c_str(), 0, false);
            
            if (rc == MOSQ_ERR_SUCCESS) {
                LOG_DEBUG("发送变化状态 [" + target_topic + "] noa_active_request=" + 
                         std::to_string(g_handshake_state.noa_active_request) + 
                         ", VID=" + g_vehicle_id);
                
                // 更新lastvalue记录
                g_handshake_state.updateLastValues();
            } else {
                LOG_ERROR("发送失败: " + std::string(mosquitto_strerror(rc)));
            }
        }
        
        // 10ms间隔
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    LOG_INFO("10ms持续发送线程退出");
}

// 信号处理
void signal_handler(int signum) {
    LOG_WARN("接收到停止信号(" + std::to_string(signum) + ")，TSP命令处理器准备退出...");
    running = false;
    should_send = false;
    
    if (mosq_client) {
        mosquitto_disconnect(mosq_client);
    }
}

// 主函数
int main() {
    // 初始化异步日志器
    initUniversalLogger("tsp_command_processor");
    
    std::cout << "启动了TSP命令处理器" << std::endl;
    std::cout << "日志已保存到通用日志系统" << std::endl;
    
    // 读取VID配置
    g_vehicle_id = readVIDFromConfig();
    
    LOG_INFO("=== TSP命令处理器 ===");
    LOG_INFO("订阅: tsp/command/10001001 → 检测(action=start/hold/stop,module=noa) → 10ms发送: /handshake/request/" + g_vehicle_id);
    LOG_INFO("发送消息: 只包含noa_active_request根据action设置(start=1, hold=0, stop=-1)");
    LOG_INFO("服务器: " + MQTT_HOST + ":" + std::to_string(MQTT_PORT));
    LOG_INFO("VID: " + g_vehicle_id);
    LOG_INFO("优化: 只赋值noa_active_request，不再处理remote_override_status和remote_override_ready");
    
    // 注册信号处理器
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化mosquitto库
    mosquitto_lib_init();
    LOG_INFO("Mosquitto库初始化完成");
    
    // 创建mosquitto实例
    mosq_client = mosquitto_new("tsp_command_processor", true, nullptr);
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
    
    // 启动持续发送线程
    std::thread sender_thread(continuous_sender_thread);
    
    LOG_INFO("TSP命令处理器启动成功，等待处理命令...");
    
    // 主循环
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 清理资源
    LOG_INFO("开始清理TSP命令处理器资源...");
    
    // 等待发送线程结束
    if (sender_thread.joinable()) {
        sender_thread.join();
    }
    
    mosquitto_loop_stop(mosq_client, false);
    mosquitto_destroy(mosq_client);
    mosquitto_lib_cleanup();
    
    LOG_INFO("TSP命令处理器退出完成");
    
    // 通用日志系统清理
    cleanupUniversalLogger();
    
    return 0;
}
