/**
 * @file bridge_forwarder.cpp
 * @brief MQTT程序4: 桥接转发器
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
#include <sstream>
#include "async_logger.hpp"

// 全局变量
static struct mosquitto* mosq_client = nullptr;
static bool running = true;
std::unique_ptr<AsyncLogger> g_logger;

// 服务器配置
const std::string MQTT_HOST = "hellorobotaxi.cn";
const int MQTT_PORT = 11883;

// 4个IDL状态管理 - 每个IDL独立维护lastvalue状态 + 新消息检测
static std::mutex g_state_mutex;
struct IDLState {
    std::map<std::string, double> values;
    uint64_t last_update_time;
    bool initialized;
    bool has_new_message;    // 新消息检测标识
    uint64_t message_count;  // 消息计数器
    
    IDLState() : last_update_time(0), initialized(false), has_new_message(false), message_count(0) {}
};

static std::map<std::string, IDLState> g_idl_states;

// 4个IDL默认状态模板
static const std::map<std::string, std::map<std::string, double>> g_idl_defaults = {
    {"/handshake/request", {{"noa_active", 1.0}, {"override_status", 0.0}, {"override_ready", 1.0}}},
    {"/handshake/response", {{"noa_active", 1.0}, {"override_response", 0.0}, {"control_source", 1.0}}},
    {"/vehicle/vehicle_status", {{"vehicle_id", 12345.0}, {"speed", 60.5}, {"control_mode", 2.0}, {"position_longitude", 116.397}, {"position_latitude", 39.916}, {"battery", 85.0}}},
    {"/vehicle/control_cmd", {{"steering_angle", 15.5}, {"throttle", 0.3}, {"brake", 0.0}, {"gear_position", 4.0}}}
};

// 4个IDL发送顺序
static const std::vector<std::string> g_idl_order = {
    "/handshake/request", "/handshake/response", "/vehicle/vehicle_status", "/vehicle/control_cmd"
};

// 发送频率配置 - 4个IDL轮流，总频率400Hz，每个IDL 100Hz
const int TARGET_FREQ_HZ = 400; // 总频率
const int TARGET_INTERVAL_MS = 1000 / TARGET_FREQ_HZ; // 2.5ms间隔

// 时间戳工具
uint64_t getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// JSON消息构建函数（参考mqtt_sender）
std::string buildJsonMessage(const std::string& topic, const std::map<std::string, double>& params) {
    std::ostringstream json;
    json << "{";
    
    bool first = true;
    for (const auto& [key, value] : params) {
        if (!first) json << ",";
        first = false;
        
        json << "\"" << key << "\":";
        
        // 处理布尔值和数值
        if (key.find("enable") != std::string::npos || 
            key.find("active") != std::string::npos || 
            key.find("ready") != std::string::npos ||
            key.find("source") != std::string::npos) {
            json << (int)value; // 布尔值作为整数
        } else if (key == "vehicle_id" || key.find("status") != std::string::npos ||
                   key.find("mode") != std::string::npos || key.find("gear") != std::string::npos) {
            json << (int)value; // 整数值
        } else {
            json << value; // 浮点数值
        }
    }
    
    // 总是添加时间戳
    if (!first) json << ",";
    json << "\"timestamp\":" << getCurrentTimestamp();
    
    json << "}";
    return json.str();
}

// 初始化所有IDL状态为默认值
void initializeIDLStates() {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    for (const auto& [topic, default_values] : g_idl_defaults) {
        g_idl_states[topic].values = default_values;
        g_idl_states[topic].initialized = true;
        g_idl_states[topic].last_update_time = getCurrentTimestamp();
        LOG_INFO("初始化IDL状态: " + topic);
    }
}

// 通用IDL状态更新函数 (服务器新消息处理)
void updateIDLState(const std::string& topic, const std::map<std::string, double>& updates) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    if (g_idl_states.find(topic) == g_idl_states.end()) {
        // 如果状态不存在，用默认值初始化
        if (g_idl_defaults.find(topic) != g_idl_defaults.end()) {
            g_idl_states[topic].values = g_idl_defaults.at(topic);
            g_idl_states[topic].initialized = true;
        }
    }
    
    // 检测是否有实际的数据变化
    bool has_changes = false;
    for (const auto& [key, value] : updates) {
        if (g_idl_states[topic].values[key] != value) {
            has_changes = true;
            g_idl_states[topic].values[key] = value;
        }
    }
    
    if (has_changes) {
        g_idl_states[topic].last_update_time = getCurrentTimestamp();
        g_idl_states[topic].has_new_message = true;  // 标记为新消息
        g_idl_states[topic].message_count++;
        LOG_INFO("[UPDATE] 服务器新消息: " + topic + " (" + std::to_string(updates.size()) + " 字段更新)");
    } else {
        LOG_DEBUG("[UNCHANGED] 服务器消息无变化: " + topic);
    }
}

// handshake/request的JSON状态更新（保持兼容现有接口）
void updateHandshakeState(const std::string& json_state) {
    // 解析JSON并更新到统一状态管理
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    std::istringstream s(json_state);
    
    if (Json::parseFromStream(builder, s, &root, &errs)) {
        std::map<std::string, double> updates;
        for (const auto& member : root.getMemberNames()) {
            if (member != "timestamp") { // 跳过时间戳字段
                if (root[member].isDouble()) {
                    updates[member] = root[member].asDouble();
                } else if (root[member].isInt()) {
                    updates[member] = (double)root[member].asInt();
                }
            }
        }
        updateIDLState("/handshake/request", updates);
    }
}

// 检测指定IDL是否有新消息
bool hasNewMessage(const std::string& topic) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    if (g_idl_states.find(topic) != g_idl_states.end()) {
        return g_idl_states[topic].has_new_message;
    }
    return false;
}

// 获取指定IDL的当前状态 + 清除新消息标识
std::pair<std::map<std::string, double>, bool> getIDLStateAndClearFlag(const std::string& topic) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    bool had_new_message = false;
    std::map<std::string, double> state;
    
    if (g_idl_states.find(topic) != g_idl_states.end()) {
        state = g_idl_states[topic].values;
        had_new_message = g_idl_states[topic].has_new_message;
        g_idl_states[topic].has_new_message = false;  // 清除新消息标识
    } else if (g_idl_defaults.find(topic) != g_idl_defaults.end()) {
        // 如果状态不存在，返回默认值
        state = g_idl_defaults.at(topic);
    }
    
    return {state, had_new_message};
}

// 获取指定IDL的当前状态 (兼容旧接口)
std::map<std::string, double> getIDLState(const std::string& topic) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    if (g_idl_states.find(topic) != g_idl_states.end()) {
        return g_idl_states[topic].values;
    }
    
    // 如果状态不存在，返回默认值
    if (g_idl_defaults.find(topic) != g_idl_defaults.end()) {
        return g_idl_defaults.at(topic);
    }
    
    return {}; // 空状态
}

// MQTT回调函数
void on_connect(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        LOG_INFO("桥接转发器连接服务器成功: " + MQTT_HOST + ":" + std::to_string(MQTT_PORT));
        
        int mid;
        int rc = mosquitto_subscribe(mosq, &mid, "/handshake/request/noa", 0);
        if (rc == MOSQ_ERR_SUCCESS) {
            LOG_INFO("订阅状态主题: /handshake/request/noa");
        } else {
            LOG_ERROR("订阅失败: " + std::string(mosquitto_strerror(rc)));
        }
    } else {
        LOG_ERROR("桥接转发器连接失败: " + std::string(mosquitto_connack_string(result)));
    }
}

void on_subscribe(struct mosquitto* mosq, void* userdata, int mid, int qos_count, const int* granted_qos) {
    LOG_INFO("桥接转发器订阅确认 (mid: " + std::to_string(mid) + ")");
}

void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    if (!message->payload) return;
    
    std::string topic = message->topic;
    std::string payload = (char*)message->payload;
    
    LOG_INFO("接收到状态更新 [" + topic + "]: " + payload);
    
    // 更新状态
    updateHandshakeState(payload);
}

void on_disconnect(struct mosquitto* mosq, void* userdata, int reason) {
    if (reason == 0) {
        LOG_INFO("桥接转发器正常断开连接");
    } else {
        LOG_ERROR("桥接转发器意外断开: " + std::string(mosquitto_strerror(reason)));
    }
}

// 400Hz轮流发送线程 - 4个IDL + 新消息检测，每个IDL 100Hz
void forwardingThread() {
    LOG_INFO("400Hz轮流发送线程启动 - 4个IDL + 新消息检测，每个100Hz");
    
    uint64_t message_count = 0;
    uint64_t last_stats_time = getCurrentTimestamp();
    
    while (running) {
        auto loop_start = std::chrono::high_resolution_clock::now();
        
        // 轮流发送4个IDL
        int topic_index = message_count % 4;
        std::string topic = g_idl_order[topic_index];
        
        // 检测服务器新消息 + 获取当前状态（lastvalue机制）
        auto [current_state, had_new_message] = getIDLStateAndClearFlag(topic);
        
        // 构建JSON消息
        std::string message_json = buildJsonMessage(topic, current_state);
        std::string target_topic = topic;
        
        // 发送消息到mqtt_to_fastdds_bridge
        if (!message_json.empty()) {
            int rc = mosquitto_publish(mosq_client, nullptr, target_topic.c_str(), 
                                      message_json.length(), message_json.c_str(), 0, false);
            
            if (rc == MOSQ_ERR_SUCCESS) {
                message_count++;
                
                // 根据新消息检测结果显示不同的日志
                if (had_new_message) {
                    LOG_DEBUG("[NEW] 发送新消息 [" + target_topic + "]: " + message_json);
                } else {
                    LOG_DEBUG("[DEFAULT] 发送默认值 [" + target_topic + "]: " + message_json);
                }
                
                // 每秒显示统计信息  
                uint64_t current_time = getCurrentTimestamp();
                if (current_time - last_stats_time >= 1000) {
                    LOG_INFO("轮流发送中: 已发送 " + std::to_string(message_count) + 
                            " 条消息 (400Hz总频率，每个IDL 100Hz)");
                    last_stats_time = current_time;
                }
            } else {
                LOG_ERROR("发送失败 [" + target_topic + "]: " + std::string(mosquitto_strerror(rc)));
            }
        }
        
        // 精确控制400Hz频率
        auto loop_end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(loop_end - loop_start);
        auto sleep_time = TARGET_INTERVAL_MS - elapsed.count();
        
        if (sleep_time > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        }
    }
    
    LOG_INFO("轮流发送线程退出");
}

// 信号处理
void signal_handler(int signum) {
    LOG_WARN("接收到停止信号(" + std::to_string(signum) + ")，桥接转发器准备退出...");
    running = false;
    
    if (mosq_client) {
        mosquitto_disconnect(mosq_client);
    }
}

// 主函数
int main() {
    // 初始化异步日志器
    g_logger = std::make_unique<AsyncLogger>("bridge_forwarder");
    
    std::cout << "启动了桥接转发器" << std::endl;
    std::cout << "日志文件在 " << g_logger->get_log_path() << std::endl;
    
    LOG_INFO("=== 车载数据聚合器 (MQTT程序4) + 新消息检测 ===");
    LOG_INFO("功能: 4个IDL状态管理 + 服务器新消息检测 + lastvalue机制");
    LOG_INFO("1. handshake/request: [ACTIVE] 有状态源 (/handshake/request/noa)");
    LOG_INFO("2. handshake/response: [DEFAULT] 暂用默认值 (可扩展状态源)");
    LOG_INFO("3. vehicle/vehicle_status: [DEFAULT] 暂用默认值 (可扩展状态源)");  
    LOG_INFO("4. vehicle/control_cmd: [DEFAULT] 暂用默认值 (可扩展状态源)");
    LOG_INFO("检测机制: 服务器有新消息->立即更新发送, 无新消息->发送默认值");
    LOG_INFO("发送频率: 400Hz轮流发送 (每个IDL 100Hz)");
    LOG_INFO("服务器: " + MQTT_HOST + ":" + std::to_string(MQTT_PORT));
    
    // 注册信号处理器
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化mosquitto库
    mosquitto_lib_init();
    LOG_INFO("Mosquitto库初始化完成");
    
    // 初始化所有IDL状态为默认值
    initializeIDLStates();
    
    // 创建mosquitto实例
    mosq_client = mosquitto_new("bridge_forwarder", true, nullptr);
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
    
    LOG_INFO("车载数据聚合器准备就绪，4个IDL状态已初始化，开始400Hz轮流发送...");
    
    // 启动400Hz轮流发送线程
    std::thread forwarding_thread(forwardingThread);
    
    // 主循环
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 等待转发线程结束
    if (forwarding_thread.joinable()) {
        forwarding_thread.join();
    }
    
    // 清理资源
    LOG_INFO("清理桥接转发器资源...");
    mosquitto_loop_stop(mosq_client, false);
    mosquitto_destroy(mosq_client);
    mosquitto_lib_cleanup();
    
    LOG_INFO("桥接转发器退出完成");
    
    // 停止异步日志器
    if (g_logger) {
        g_logger->stop();
        g_logger.reset();
    }
    
    return 0;
}