// 单向桥接转发器 - 单线程50Hz转发 (仅下行)
// 下行链路: 外部MQTT带VID → 本地MQTT不带VID (50Hz)

#include <iostream>
#include <mosquitto.h>
#include <json/json.h>
#include <chrono>
#include <thread>
#include <string>
#include <mutex>
#include <signal.h>
#include <map>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include "logger_conf/universal_logger.h"

static bool running = true;
struct mosquitto* external_mosq = nullptr;
struct mosquitto* local_mosq = nullptr;

const std::string EXTERNAL_HOST = "hellorobotaxi.cn";
const int EXTERNAL_PORT = 11883;
// const std::string EXTERNAL_HOST = "192.168.2.90";
const std::string LOCAL_HOST = "127.0.0.1";
const int LOCAL_PORT = 1883;
std::string g_vehicle_id = "10001001";

static std::mutex g_state_mutex;
static std::map<std::string, std::map<std::string, double>> g_message_states;
static std::map<std::string, bool> g_message_updated;
static std::map<std::string, bool> g_value_changed;
static std::map<std::string, int> g_lastvalue_count;
static std::map<std::string, int> g_duplicate_count;
static bool g_should_send_messages = false;

std::string get_topic_log_path(const std::string& topic_name) {
    std::string clean_topic = topic_name;
    std::string vid_suffix = "/" + g_vehicle_id;
    
    if (topic_name.size() > vid_suffix.size() && 
        topic_name.substr(topic_name.size() - vid_suffix.size()) == vid_suffix) {
        clean_topic = topic_name.substr(0, topic_name.size() - vid_suffix.size());
    }
    
    if (clean_topic == "/handshake/request") return "handshake/request";
    if (clean_topic == "/vehicle/control_cmd") return "vehicle/control_cmd";
    
    if (clean_topic.front() == '/') clean_topic = clean_topic.substr(1);
    std::replace(clean_topic.begin(), clean_topic.end(), '/', '_');
    return "general/" + clean_topic;
}

static const std::map<std::string, std::map<std::string, double>> g_idl_defaults = {
    {"/handshake/request", {
        {"noa_active_request", 0.0},
        {"remote_override_status", 0.0},
        {"remote_override_ready", 0.0}
    }},
    {"/vehicle/control_cmd", {
        {"steering_angle_enable", 0.0}, {"steering_angle", 0.0},
        {"target_acceleration_enable", 0.0}, {"target_acceleration", 0.0},
        {"indicator_left_enable", 0.0}, {"indicator_left", 0.0},
        {"indicator_right_enable", 0.0}, {"indicator_right", 0.0},
        {"gear_position_enable", 0.0}, {"gear_position", 0.0},
        {"ebrake_status_enable", 0.0}, {"ebrake_status", 0.0},
        {"lane_change", 0.0}
    }}
};

void initMessageStates() {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    for (const auto& [topic, defaults] : g_idl_defaults) {
        g_message_updated[topic] = false;
        g_value_changed[topic] = false;
        g_lastvalue_count[topic] = 0;
        g_duplicate_count[topic] = 0;
        
        g_message_states[topic] = defaults;
        g_message_updated[topic] = true;
        g_value_changed[topic] = true;
    }
    
    g_should_send_messages = true;
    std::cout << "[SUCCESS] 消息状态初始化完成，将发送全0初始值（非retain）" << std::endl;
}

std::string readVIDFromConfig() {
    std::ifstream file("../car_config/vehicle_config.yaml");
    if (!file.is_open()) return "12345678";
    
    std::string line;
    while (std::getline(file, line)) {
        size_t vid_pos = line.find("vid:");
        if (vid_pos != std::string::npos) {
            size_t quote_start = line.find("\"", vid_pos);
            if (quote_start != std::string::npos) {
                size_t quote_end = line.find("\"", quote_start + 1);
                if (quote_end != std::string::npos) {
                    return line.substr(quote_start + 1, quote_end - quote_start - 1);
                }
            }
        }
    }
    return "12345678";
}

void updateMessageState(const std::string& topic, const std::map<std::string, double>& new_values, const std::string& original_payload = "") {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    if (g_message_states.find(topic) == g_message_states.end()) {
        g_message_states[topic] = std::map<std::string, double>();
    }
    
    bool has_any_change = false;
    for (const auto& [key, value] : new_values) {
        if (key.find("timestamp") != std::string::npos) continue;
        if (g_message_states[topic].find(key) == g_message_states[topic].end() || 
            g_message_states[topic][key] != value) {
            has_any_change = true;
            break;
        }
    }
    
    if (!has_any_change && !g_message_states[topic].empty()) {
        g_duplicate_count[topic]++;
        if (g_duplicate_count[topic] % 100 == 0) {
            std::cout << "[DUPLICATE] " << topic << " 已丢弃 " << g_duplicate_count[topic] << " 条重复消息" << std::endl;
        }
        return;
    }
    
    for (const auto& [key, value] : new_values) {
        g_message_states[topic][key] = value;
    }
    
    g_message_updated[topic] = true;
    g_value_changed[topic] = has_any_change;
    g_should_send_messages = true;
    
    if (has_any_change) {
        g_lastvalue_count[topic] = 0;
        
        // 打印收到的原始消息（仅在值改变时）
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        
        std::tm* tm_now = std::localtime(&time_t_now);
        std::cout << "[" 
                  << std::setfill('0') << std::setw(2) << tm_now->tm_hour << ":"
                  << std::setfill('0') << std::setw(2) << tm_now->tm_min << ":"
                  << std::setfill('0') << std::setw(2) << tm_now->tm_sec << "."
                  << std::setfill('0') << std::setw(3) << ms.count()
                  << "] topic: " << topic << " payload: " << original_payload << std::endl;
        
        // 构建更新值的JSON
        Json::Value updated_json;
        for (const auto& [key, value] : new_values) {
            if (key.find("timestamp") == std::string::npos) {
                updated_json[key] = value;
            }
        }
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string updated_str = Json::writeString(builder, updated_json);
        if (!updated_str.empty() && updated_str.back() == '\n') {
            updated_str.pop_back();
        }
        
        std::cout << "[UPDATE] " << topic << " 值已更新 " << updated_str << std::endl;
    }
}

uint64_t getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

std::string buildJsonMessage(const std::string& topic, const std::map<std::string, double>& values) {
    Json::Value json_obj;
    for (const auto& [key, value] : values) {
        // 所有追踪时间戳需要保持为整数类型
        if (key == "vehicle_instruction_takeover_timestamp" || key == "mqtt_sender_timestamp") {
            json_obj[key] = static_cast<Json::Int64>(value);
        } else {
            json_obj[key] = value;
        }
    }
    json_obj["timestamp"] = static_cast<Json::Int64>(getCurrentTimestamp());
    json_obj["bridge_forwarder_timestamp"] = static_cast<Json::Int64>(getCurrentTimestamp());
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, json_obj);
}

bool publishMessage(struct mosquitto* mosq, const std::string& topic, const std::string& message) {
    if (!mosq) return false;
    return mosquitto_publish(mosq, nullptr, topic.c_str(), message.length(), message.c_str(), 0, false) == MOSQ_ERR_SUCCESS;
}

void handleUnifiedMessage(const std::string& topic, const std::string& payload) {
    try {
        Json::Value json_data;
        Json::Reader reader;
        
        if (reader.parse(payload, json_data)) {
            std::map<std::string, double> new_values;
            
            for (const auto& member_name : json_data.getMemberNames()) {
                if (json_data[member_name].isNumeric()) {
                    new_values[member_name] = json_data[member_name].asDouble();
                } else if (json_data[member_name].isBool()) {
                    new_values[member_name] = json_data[member_name].asBool() ? 1.0 : 0.0;
                }
            }
            
            // 特别保留所有追踪时间戳用于延迟分析
            if (json_data.isMember("vehicle_instruction_takeover_timestamp") && json_data["vehicle_instruction_takeover_timestamp"].isNumeric()) {
                new_values["vehicle_instruction_takeover_timestamp"] = json_data["vehicle_instruction_takeover_timestamp"].asDouble();
            }
            if (json_data.isMember("mqtt_sender_timestamp") && json_data["mqtt_sender_timestamp"].isNumeric()) {
                new_values["mqtt_sender_timestamp"] = json_data["mqtt_sender_timestamp"].asDouble();
            }
            
            std::string target_topic = topic;
            std::string vid_suffix = "/" + g_vehicle_id;
            
            if (topic.size() > vid_suffix.size() && 
                topic.substr(topic.size() - vid_suffix.size()) == vid_suffix) {
                target_topic = topic.substr(0, topic.size() - vid_suffix.size());
            }
            
            updateMessageState(target_topic, new_values, payload);
            LOG_BY_TOPIC(get_topic_log_path(target_topic), "Message received: " + target_topic + " | Payload: " + payload);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("消息解析失败 [" + topic + "]: " + std::string(e.what()));
    }
}


bool processForwardLink(uint64_t& message_count) {
    std::map<std::string, std::map<std::string, double>> messages_to_send;
    
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (!g_should_send_messages) return false;
        
        for (const auto& [topic, updated] : g_message_updated) {
            if (updated && g_message_states.find(topic) != g_message_states.end()) {
                messages_to_send[topic] = g_message_states[topic];
            }
        }
    }
    
    for (const auto& [topic, current_state] : messages_to_send) {
        std::string message = buildJsonMessage(topic, current_state);
        
        if (publishMessage(local_mosq, topic, message)) {
            message_count++;
            
            {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                if (!g_value_changed[topic]) {
                    g_lastvalue_count[topic]++;
                } else {
                    g_value_changed[topic] = false;
                }
            }
            
            if (message_count % 100 == 0) {
                LOG_INFO("下行链路: 已转发 " + std::to_string(message_count) + " 条消息 (50Hz)");
            }
        }
    }
    
    return !messages_to_send.empty();
}

void on_connect_external(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        std::string handshake_topic = "/handshake/request/" + g_vehicle_id;
        std::string control_topic = "/vehicle/control_cmd/" + g_vehicle_id;
        
        mosquitto_subscribe(mosq, nullptr, handshake_topic.c_str(), 0);
        mosquitto_subscribe(mosq, nullptr, control_topic.c_str(), 0);
        
        LOG_INFO("外部MQTT连接成功，已订阅话题");
    } else {
        std::cout << "[ERROR] 外部MQTT连接失败" << std::endl;
    }
}

void on_connect_local(struct mosquitto* mosq, void* userdata, int result) {
    if (result != 0) {
        std::cout << "[ERROR] 本地MQTT连接失败" << std::endl;
    }
}

void on_message_external(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    std::string topic = message->topic;
    std::string payload(static_cast<char*>(message->payload), message->payloadlen);
    
    // 只处理消息，不在这里打印（在updateMessageState中当值改变时才打印）
    handleUnifiedMessage(topic, payload);
}

void on_message_local(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    // 上行功能已禁用 - 本地MQTT仅用于发布下行消息
}

bool initMQTT() {
    mosquitto_lib_init();
    
    external_mosq = mosquitto_new("bridge_forwarder_external_2", true, nullptr);
    if (!external_mosq) {
        std::cout << "[ERROR] 创建外部MQTT客户端失败" << std::endl;
        return false;
    }
    mosquitto_connect_callback_set(external_mosq, on_connect_external);
    mosquitto_message_callback_set(external_mosq, on_message_external);
    
    local_mosq = mosquitto_new("bridge_forwarder_local_2", true, nullptr);
    if (!local_mosq) {
        std::cout << "[ERROR] 创建本地MQTT客户端失败" << std::endl;
        return false;
    }
    mosquitto_connect_callback_set(local_mosq, on_connect_local);
    mosquitto_message_callback_set(local_mosq, on_message_local);
    
    if (mosquitto_connect(external_mosq, EXTERNAL_HOST.c_str(), EXTERNAL_PORT, 60) != MOSQ_ERR_SUCCESS) {
        std::cout << "[ERROR] 连接外部MQTT失败" << std::endl;
        return false;
    }
    
    if (mosquitto_connect(local_mosq, LOCAL_HOST.c_str(), LOCAL_PORT, 60) != MOSQ_ERR_SUCCESS) {
        std::cout << "[ERROR] 连接本地MQTT失败" << std::endl;
        return false;
    }
    
    std::cout << "[INFO] MQTT连接成功" << std::endl;
    return true;
}

void cleanup() {
    running = false;
    
    if (external_mosq) {
        mosquitto_destroy(external_mosq);
    }
    
    if (local_mosq) {
        mosquitto_destroy(local_mosq);
    }
    
    mosquitto_lib_cleanup();
    std::cout << "[SUCCESS] 清理完成" << std::endl;
}

void signal_handler(int sig) {
    std::cout << "\n接收到停止信号，准备退出..." << std::endl;
    cleanup();
    cleanupUniversalLogger();
    exit(0);
}

int main() {
    std::cout << "单向MQTT桥接转发器 - 50Hz转发 (仅下行)" << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    g_vehicle_id = readVIDFromConfig();
    initUniversalLogger("bridge_forwarder");
    initMessageStates();
    
    if (!initMQTT()) {
        cleanup();
        return 1;
    }
    
    std::cout << "[SUCCESS] 桥接转发器启动成功, VID=" << g_vehicle_id << std::endl;
    
    uint64_t message_count = 0;
    const auto forward_interval = std::chrono::microseconds(20000);
    auto last_forward_time = std::chrono::high_resolution_clock::now();

    while (running) {
        // 使用非阻塞模式处理MQTT消息，超时时间设为0
        mosquitto_loop(external_mosq, 0, 1);
        mosquitto_loop(local_mosq, 0, 1);
        
        auto now = std::chrono::high_resolution_clock::now();
        if (now - last_forward_time >= forward_interval) {
            processForwardLink(message_count);
            last_forward_time = now;
        }
        
        // 使用更短的sleep时间，提高消息处理响应性
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    cleanup();
    cleanupUniversalLogger();
    return 0;
}