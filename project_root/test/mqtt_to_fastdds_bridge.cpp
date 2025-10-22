// MQTT→FastDDS桥接器: 127.0.0.1:1883 → FastDDS

#include "../fastdds_lib/include/mqtt_to_fastdds_converter.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <sstream>
#include <signal.h>
#include <atomic>
#include <map>
#include <vector>
#include <algorithm>
#include <json/json.h>
#include "logger_conf/universal_logger.h"

std::string get_topic_log_path(const std::string& topic) {
    if (topic.find("/handshake/request") == 0) return "handshake/request";
    if (topic.find("/vehicle/control_cmd") == 0) return "vehicle/control_cmd";
    std::string path = topic;
    if (!path.empty() && path.front() == '/') path = path.substr(1);
    std::replace(path.begin(), path.end(), '/', '_');
    return "bridge/" + path;
}

std::unique_ptr<MQTTToFastDDSConverter> g_converter = nullptr;
std::atomic<bool> g_running{true};


void on_mqtt_message_received(const std::string& topic, const std::string& message, bool success) {
    static std::map<std::string, std::string> last_msg_values;
    
    // 提取非timestamp字段用于比较
    Json::Value json;
    Json::Reader reader;
    std::string current_values;
    bool value_changed = false;
    
    if (reader.parse(message, json)) {
        Json::Value compare_json;
        for (const auto& name : json.getMemberNames()) {
            if (name.find("timestamp") == std::string::npos) {
                compare_json[name] = json[name];
            }
        }
        
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        current_values = Json::writeString(builder, compare_json);
        if (!current_values.empty() && current_values.back() == '\n') {
            current_values.pop_back();
        }
        
        // 比较非timestamp字段是否变化，只在变化时打印
        if (last_msg_values[topic] != current_values) {
            value_changed = true;
            last_msg_values[topic] = current_values;
            std::cout << "[UPDATE] Topic: " << topic << " | 值已更新: " << current_values << std::endl;
            
            // 当前时间（mqtt_to_fastdds接收时间）
            uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            // 收集所有时间戳（按顺序）
            std::vector<std::pair<std::string, uint64_t>> timestamps;
            
            if (json.isMember("vehicle_instruction_takeover_timestamp")) {
                timestamps.push_back({"vehicle_instruction_takeover_timestamp", json["vehicle_instruction_takeover_timestamp"].asUInt64()});
            }
            if (json.isMember("mqtt_sender_timestamp")) {
                timestamps.push_back({"mqtt_sender_timestamp", json["mqtt_sender_timestamp"].asUInt64()});
            }
            if (json.isMember("mqtt_send_timestamp")) {
                timestamps.push_back({"mqtt_send_timestamp", json["mqtt_send_timestamp"].asUInt64()});
            }
            if (json.isMember("bridge_forwarder_timestamp")) {
                timestamps.push_back({"bridge_forwarder_timestamp", json["bridge_forwarder_timestamp"].asUInt64()});
            }
            if (json.isMember("timestamp")) {
                timestamps.push_back({"timestamp", json["timestamp"].asUInt64()});
            }
            timestamps.push_back({"mqtt_to_fastdds_timestamp", current_time});
            
            // 打印所有时间戳
            std::cout << "  [时间戳信息] " << topic << std::endl;
            for (const auto& [name, ts] : timestamps) {
                std::cout << "    " << name;
                // 对齐填充空格
                int padding = 40 - name.length();
                for (int i = 0; i < padding; i++) std::cout << " ";
                std::cout << ": " << ts << std::endl;
            }
            
            // 计算每段延迟
            if (timestamps.size() >= 2) {
                std::cout << "  [延迟分析]" << std::endl;
                uint64_t total_latency = 0;
                
                for (size_t i = 1; i < timestamps.size(); i++) {
                    uint64_t latency = timestamps[i].second - timestamps[i-1].second;
                    total_latency += latency;
                    
                    // 简化名称用于显示
                    std::string from = timestamps[i-1].first;
                    std::string to = timestamps[i].first;
                    
                    // 去掉_timestamp后缀
                    if (from.find("_timestamp") != std::string::npos) {
                        from = from.substr(0, from.find("_timestamp"));
                    }
                    if (to.find("_timestamp") != std::string::npos) {
                        to = to.substr(0, to.find("_timestamp"));
                    }
                    
                    std::cout << "    " << from << " → " << to << " : " << latency << "ms" << std::endl;
                }
                
                if (timestamps.size() > 2) {
                    std::cout << "    -------------------------------------------------------" << std::endl;
                    std::cout << "    总延迟 : " << total_latency << "ms" << std::endl;
                }
            }
            
            std::cout << std::endl;
        }
    }
    
    LOG_BY_TOPIC(get_topic_log_path(topic), 
                 "MQTT→FastDDS " + std::string(success ? "✓" : "✗") + " " + message);
}

void signal_handler(int sig) {
    static int count = 0;
    if (++count > 1) exit(1);
    g_running = false;
}

int main() {
    std::cout << "MQTT→FastDDS桥接器" << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    initUniversalLogger("mqtt_to_fastdds_bridge");
    
    g_converter = std::make_unique<MQTTToFastDDSConverter>(0, "127.0.0.1", 1883, "mqtt_to_fastdds_bridge");
    g_converter->setMQTTMessageCallback(on_mqtt_message_received);
    
    if (!g_converter->init()) {
        std::cerr << "❌ 初始化失败" << std::endl;
        return 1;
    }

    std::cout << "✅ 已启动 (监听 /handshake/request + /vehicle/control_cmd)" << std::endl;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    g_converter.reset();
    cleanupUniversalLogger();
    return 0;
}