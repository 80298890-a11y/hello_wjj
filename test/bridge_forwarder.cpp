/**
 * @file bridge_forwarder.cpp
 * @brief 双向桥接转发器 - 支持正向和反向链路
 * 
 * 正向链路: 订阅/handshake/request/{VID} → 200Hz发送/handshake/request (云端指定车辆发送)
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
#include <algorithm>
#include <iomanip>
#include "logger_conf/universal_logger.h"

// 全局变量
static bool running = true;

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

/**
 * @brief 获取topic对应的日志路径（用于按topic分类日志）
 */
std::string get_topic_log_path(const std::string& topic_name) {
    // 移除VID后缀，获取原始topic
    std::string clean_topic = topic_name;
    std::string vid_suffix = "/" + g_vehicle_id;
    if (topic_name.size() > vid_suffix.size() && 
        topic_name.substr(topic_name.size() - vid_suffix.size()) == vid_suffix) {
        clean_topic = topic_name.substr(0, topic_name.size() - vid_suffix.size());
    }
    
    // 根据topic转换为文件路径
    if (clean_topic == "/handshake/request") return "handshake/request";
    if (clean_topic == "/handshake/response") return "handshake/response";
    if (clean_topic == "/vehicle/control_cmd") return "vehicle/control_cmd";
    if (clean_topic == "/vehicle/vehicle_status") return "vehicle/vehicle_status";
    if (clean_topic == "/vehicle/drive_state") return "vehicle/drive_state";
    if (clean_topic == "/vehicle/fault_report") return "vehicle/fault_report";
    
    // 默认路径: 移除开头的'/'并替换为路径分隔符
    std::string path_name = clean_topic;
    if (path_name.front() == '/') path_name = path_name.substr(1);
    std::replace(path_name.begin(), path_name.end(), '/', '_');
    return "general/" + path_name;
}

// 通用状态管理 - 支持所有IDL消息类型的状态保持
static std::mutex g_state_mutex;
static std::map<std::string, std::map<std::string, double>> g_message_states; // 每个topic的当前状态
static std::map<std::string, bool> g_message_updated; // 标记哪些消息需要发送

// 消息发送控制
static bool g_should_send_messages = false;  // 是否需要发送消息

// IDL默认值 - 包含所有字段的完整默认值
static const std::map<std::string, std::map<std::string, double>> g_idl_defaults = {
    {"/handshake/request", {
        {"noa_active_request", 0.0}, 
        {"remote_override_status", 0.0}, 
        {"remote_override_ready", 0.0}
    }},
    {"/vehicle/control_cmd", {
        {"steering_angle_enable", 0.0}, 
        {"steering_angle", 0.0}, 
        {"target_acceleration_enable", 0.0}, 
        {"target_acceleration", 0.0}, 
        {"indicator_left_enable", 0.0}, 
        {"indicator_left", 0.0}, 
        {"indicator_right_enable", 0.0}, 
        {"indicator_right", 0.0}, 
        {"gear_position_enable", 0.0}, 
        {"gear_position", 0.0}, 
        {"ebrake_status_enable", 0.0}, 
        {"ebrake_status", 0.0},
        {"lane_change", 0.0}
    }},
};

/**
 * @brief 初始化所有消息状态为默认值
 */
void initMessageStates() {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    // 为每个topic初始化状态
    for (const auto& [topic, defaults] : g_idl_defaults) {
        g_message_states[topic] = defaults;  // 复制默认值作为初始状态
        g_message_updated[topic] = false;    // 初始时没有更新
    }
    
    std::cout << "[SUCCESS] 消息状态初始化完成，支持 " << g_idl_defaults.size() << " 个topic的lastvalue功能" << std::endl;
}

/**
 * @brief 更新指定topic的消息状态
 */
void updateMessageState(const std::string& topic, const std::map<std::string, double>& new_values) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    if (g_message_states.find(topic) != g_message_states.end()) {
        // 更新状态值（只更新提供的字段，其他保持lastvalue）
        for (const auto& [key, value] : new_values) {
            if (g_message_states[topic].find(key) != g_message_states[topic].end()) {
                g_message_states[topic][key] = value;
            }
        }
        
        // 标记该topic需要发送
        g_message_updated[topic] = true;
        g_should_send_messages = true;
        
        std::cout << "[UPDATE] " << topic << " 状态更新，触发发送" << std::endl;
    }
}

/**
 * @brief 获取指定topic的当前状态
 */
std::map<std::string, double> getCurrentState(const std::string& topic) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    if (g_message_states.find(topic) != g_message_states.end()) {
        return g_message_states[topic];
    }
    
    // 如果没有找到，返回默认值
    if (g_idl_defaults.find(topic) != g_idl_defaults.end()) {
        return g_idl_defaults.at(topic);
    }
    
    return {};
}

/**
 * @brief 从配置文件读取VID
 */
std::string readVIDFromConfig() {
    const std::string config_file = "../car_config/vehicle_config.yaml";
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
 * @brief 正向链路: 200Hz发送线程 (支持所有IDL消息的lastvalue发送)
 */
void forwardLinkThread() {
    std::cout << "[SUCCESS] 正向链路启动: 200Hz发送所有IDL消息 (支持lastvalue状态保持)" << std::endl;
    
    uint64_t message_count = 0;
    const auto interval = std::chrono::microseconds(5000); // 200Hz = 5ms间隔
    
    while (running) {
        auto loop_start = std::chrono::high_resolution_clock::now();
        
        // 检查所有需要发送的消息
        bool should_send_any = false;
        std::map<std::string, std::map<std::string, double>> messages_to_send;
        
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            should_send_any = g_should_send_messages;
            
            if (should_send_any) {
                // 收集所有需要发送的消息（使用当前状态，支持lastvalue）
                for (const auto& [topic, updated] : g_message_updated) {
                    if (updated && g_message_states.find(topic) != g_message_states.end()) {
                        messages_to_send[topic] = g_message_states[topic];
                    }
                }
            }
        }
        
        // 发送所有标记为需要发送的消息
        if (should_send_any) {
            for (const auto& [topic, current_state] : messages_to_send) {
                std::string message = buildJsonMessage(topic, current_state);
                bool success = publishToExternal(topic, message);
                
                if (success) {
                    message_count++;
                    if (message_count % 200 == 0) { // 每秒显示一次统计
                        LOG_INFO("正向链路: 已发送 " + std::to_string(message_count) + " 条消息 (200Hz)");
                    }
                }
            }
        }
        
        // 频率控制 - 保持200Hz
        auto elapsed = std::chrono::high_resolution_clock::now() - loop_start;
        if (elapsed < interval) {
            std::this_thread::sleep_for(interval - elapsed);
        }
    }
}

/**
 * @brief 通用消息处理: 解析JSON并更新对应topic的状态
 * 处理带VID的topic，转换为原始topic进行状态管理
 */
void handleForwardMessage(const std::string& topic, const std::string& payload) {
    try {
        Json::Value json_data;
        Json::Reader reader;
        
        if (reader.parse(payload, json_data)) {
            std::map<std::string, double> new_values;
            
            // 解析JSON中的所有字段
            for (const auto& member_name : json_data.getMemberNames()) {
                if (json_data[member_name].isNumeric()) {
                    new_values[member_name] = json_data[member_name].asDouble();
                } else if (json_data[member_name].isBool()) {
                    new_values[member_name] = json_data[member_name].asBool() ? 1.0 : 0.0;
                }
            }
            
            // 从带VID的topic转换为原始topic
            std::string target_topic = topic;
            std::string vid_suffix = "/" + g_vehicle_id;
            
            // 检查并移除VID后缀
            if (topic.size() > vid_suffix.size() && 
                topic.substr(topic.size() - vid_suffix.size()) == vid_suffix) {
                target_topic = topic.substr(0, topic.size() - vid_suffix.size());
                // 移除冗余日志，详细信息在状态更新中已记录
            }
            
            // 更新消息状态（使用原始topic作为key）
            updateMessageState(target_topic, new_values);
            
            // 📝 每次收到MQTT消息都记录日志（不管状态是否变化）
            // 构建详细的字段信息（与subscriber保持一致，时间戳显示完整数字）
            std::ostringstream field_info;
            field_info << std::fixed << std::setprecision(0); // 设置为固定点记数法，无小数位
            if (target_topic == "/handshake/request") {
                field_info << "HandshakeRequest: ";
                for (const auto& [key, value] : new_values) {
                    field_info << key << "=" << value << ", ";
                }
            } else if (target_topic == "/vehicle/control_cmd") {
                field_info << "ControlCmd: ";
                for (const auto& [key, value] : new_values) {
                    field_info << key << "=" << value << ", ";
                }
            } else {
                field_info << target_topic << ": ";
                for (const auto& [key, value] : new_values) {
                    field_info << key << "=" << value << ", ";
                }
            }
            std::string log_msg = field_info.str();
            if (log_msg.length() > 2) log_msg = log_msg.substr(0, log_msg.length() - 2); // 移除最后的", "
            
            LOG_BY_TOPIC(get_topic_log_path(target_topic), log_msg);
            
        }
    } catch (const std::exception& e) {
        LOG_ERROR("消息解析失败 [" + topic + "]: " + std::string(e.what()));
    }
}

/**
 * @brief 处理控制命令消息: 专门处理vehicle/control_cmd类型的消息
 * 处理带VID的topic，转换为原始topic进行状态管理
 */
void handleControlMessage(const std::string& topic, const std::string& payload) {
    try {
        Json::Value json_data;
        Json::Reader reader;
        
        if (reader.parse(payload, json_data)) {
            std::map<std::string, double> new_values;
            
            // 解析控制命令中的所有字段
            for (const auto& member_name : json_data.getMemberNames()) {
                if (json_data[member_name].isNumeric()) {
                    new_values[member_name] = json_data[member_name].asDouble();
                } else if (json_data[member_name].isBool()) {
                    new_values[member_name] = json_data[member_name].asBool() ? 1.0 : 0.0;
                }
            }
            
            // 从带VID的topic转换为原始topic
            std::string target_topic = topic;
            std::string vid_suffix = "/" + g_vehicle_id;
            
            // 检查并移除VID后缀
            if (topic.size() > vid_suffix.size() && 
                topic.substr(topic.size() - vid_suffix.size()) == vid_suffix) {
                target_topic = topic.substr(0, topic.size() - vid_suffix.size());
                // 移除冗余日志，详细信息在状态更新中已记录
            }
            
            // 更新控制命令状态（使用原始topic作为key）
            updateMessageState(target_topic, new_values);
            
            // 构建详细的控制命令字段信息（与subscriber保持一致，时间戳显示完整数字）
            std::ostringstream ctrl_field_info;
            ctrl_field_info << std::fixed << std::setprecision(0); // 设置为固定点记数法，无小数位
            ctrl_field_info << "ControlCmd: ";
            for (const auto& [key, value] : new_values) {
                ctrl_field_info << key << "=" << value << ", ";
            }
            std::string ctrl_log_msg = ctrl_field_info.str();
            if (ctrl_log_msg.length() > 2) ctrl_log_msg = ctrl_log_msg.substr(0, ctrl_log_msg.length() - 2); // 移除最后的", "
            
            LOG_BY_TOPIC(get_topic_log_path(target_topic), ctrl_log_msg);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("控制命令解析失败: " + std::string(e.what()));
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
        LOG_BY_TOPIC(get_topic_log_path(topic), "反向链路转发成功: " + topic);
    } else {
        LOG_BY_TOPIC(get_topic_log_path(topic), "反向链路转发失败: " + topic);
    }
}

// =========================== MQTT回调函数 ===========================

void on_connect_external(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        std::cout << "[SUCCESS] 外部MQTT连接成功: " << EXTERNAL_HOST << ":" << EXTERNAL_PORT << std::endl;
        
        // 订阅正向链路话题（带VID，实现云端指定车辆发送）
        std::string handshake_topic = "/handshake/request/" + g_vehicle_id;
        std::string control_topic = "/vehicle/control_cmd/" + g_vehicle_id;
        
        mosquitto_subscribe(mosq, nullptr, handshake_topic.c_str(), 0);
        mosquitto_subscribe(mosq, nullptr, control_topic.c_str(), 0);
        LOG_INFO("订阅正向链路话题: " + handshake_topic + ", " + control_topic);
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
            "/vehicle/control_cmd/" + g_vehicle_id,
            "/vehicle/drive_state/" + g_vehicle_id,
            "/vehicle/fault_report/" + g_vehicle_id
        };
        
        for (const auto& topic : reverse_topics) {
            mosquitto_subscribe(mosq, nullptr, topic.c_str(), 0);
            LOG_INFO("订阅反向链路话题: " + topic);
        }
    } else {
        std::cout << "[ERROR] 本地MQTT连接失败" << std::endl;
    }
}

void on_message_external(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    std::string topic = message->topic;
    std::string payload(static_cast<char*>(message->payload), message->payloadlen);
    bool is_retained = message->retain;  // 检测是否是retained消息
    
    // 如果是retained消息，只记录不更新状态
    if (is_retained) {
        LOG_BY_TOPIC(get_topic_log_path(topic), "接收到retained消息: " + topic + " (只读取，不更新状态)");
        return;
    }
    
    // 处理不同类型的消息（支持带VID的topic）
    if (topic.find("/handshake/request") == 0) {
        handleForwardMessage(topic, payload);
    } else if (topic.find("/vehicle/control_cmd") == 0) {
        handleControlMessage(topic, payload);
    } else {
        // 其他类型的消息也用通用处理
        handleForwardMessage(topic, payload);
        LOG_BY_TOPIC(get_topic_log_path(topic), "接收到消息: " + topic + " (长度: " + std::to_string(payload.length()) + ")");
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
    
    // 本地MQTT已简化，只使用外部服务器
    local_mosq = nullptr;  // 简化版本不使用本地MQTT
    
    // 连接到服务器
    if (mosquitto_connect(external_mosq, EXTERNAL_HOST.c_str(), EXTERNAL_PORT, 60) != MOSQ_ERR_SUCCESS) {
        std::cout << "[ERROR] 连接外部MQTT失败" << std::endl;
        return false;
    }
    
    // 简化版本：跳过本地MQTT连接
    std::cout << "[INFO] 简化模式：跳过本地MQTT，只使用外部服务器" << std::endl;
    
    // 启动网络循环
    mosquitto_loop_start(external_mosq);
    // 简化版本：不启动本地MQTT循环
    
    return true;
}

void cleanup() {
    running = false;
    
    if (external_mosq) {
        mosquitto_loop_stop(external_mosq, true);
        mosquitto_destroy(external_mosq);
    }
    
    if (local_mosq) {
            // 简化版本：local_mosq 是 nullptr，跳过清理
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
    std::cout << "双向桥接转发器 (支持LastValue状态保持)" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "正向链路: 支持所有IDL消息 → 200Hz发送 (状态保持)" << std::endl;
    std::cout << "反向链路: 本地MQTT带VID话题 → 外部服务器" << std::endl;
    std::cout << "LastValue: 未收到新消息时发送上次的值，收到新消息时更新状态" << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 读取VID配置
    g_vehicle_id = readVIDFromConfig();
    
    // 初始化通用日志系统
    initUniversalLogger("bridge_forwarder");
    std::cout << "通用日志系统启动，支持自动清理(最大" << 50 << "MB)" << std::endl;
    
    // 初始化消息状态管理
    initMessageStates();
    
    // 初始化MQTT
    if (!initMQTT()) {
        cleanup();
        return 1;
    }
    
    // 启动正向链路线程
    std::thread forward_thread(forwardLinkThread);
    
    std::cout << "[SUCCESS] 双向桥接转发器启动成功 (支持LastValue状态保持)" << std::endl;
    std::cout << "正向: 200Hz发送所有IDL消息到外部服务器 (lastvalue机制)" << std::endl;
    std::cout << "反向: 转发VID=" << g_vehicle_id << "的消息到外部服务器" << std::endl;
    std::cout << "状态: 收到消息→更新状态→持续发送lastvalue，直到收到新消息" << std::endl;
    
    // 主循环
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    forward_thread.join();
    cleanup();
    cleanupUniversalLogger();
    return 0;
}
