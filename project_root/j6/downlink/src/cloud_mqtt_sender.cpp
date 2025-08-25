/**
 * @file cloud_mqtt_sender.cpp
 * @brief J6下行系统 - 云端MQTT发送器(简化版)
 * 
 * 功能：
 * - 支持所有IDL消息类型，智能默认值填充
 * - 支持单次发送、持续发送、流式发送
 * - 简化的命令行接口，500行以内
 * 
 * 使用：
 * ./cloud_mqtt_sender send <topic> [key=value...]     # 发送单条消息
 * ./cloud_mqtt_sender continuous <topic> [key=value...] # 持续100Hz发送
 * ./cloud_mqtt_sender stream <topic> [key=value...]   # 流式发送，可动态更新
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sstream>
#include <signal.h>
#include <atomic>
#include <map>
#include <mutex>
#include <algorithm>
#include <mosquitto.h>
#include "AsyncLogger.h"
#include "ConfigManager.h"

// 全局控制变量
std::atomic<bool> g_running{true};
std::atomic<bool> g_stream_running{false};

// MQTT相关全局变量
struct mosquitto* g_mosq = nullptr;
std::unique_ptr<AsyncLogger> g_logger = nullptr;
std::unique_ptr<ConfigManager> g_config = nullptr;
uint64_t g_message_sequence = 0;

// 流式发送参数
std::mutex g_stream_mutex;
std::map<std::string, double> g_stream_params;
std::string g_stream_topic;

// IDL消息模板
std::map<std::string, std::map<std::string, double>> get_templates() {
    static std::map<std::string, std::map<std::string, double>> templates = {
        {"/vehicle/control_cmd", {
            {"steering_angle_enable", 0}, {"steering_angle", 0.0},
            {"target_acceleration_enable", 0}, {"target_acceleration", 0.0},
            {"indicator_left_enable", 0}, {"indicator_left", 0},
            {"indicator_right_enable", 0}, {"indicator_right", 0},
            {"gear_position_enable", 0}, {"gear_position", 0},
            {"ebrake_status_enable", 0}, {"ebrake_status", 0}
        }},
        {"/handshake/request", {
            {"noa_active_request", 0}, {"remote_override_status", 0}, {"remote_override_ready", 0}
        }},
        {"/handshake/response", {
            {"noa_active_response", 0}, {"remote_override_response", 0}, {"current_control_source", 0}
        }},
        {"/vehicle/vehicle_status", {
            {"vehicle_id", 0}, {"control_mode", 0}, {"position_longitude", 0.0}, {"position_latitude", 0.0},
            {"speed", 0.0}, {"heading", 0.0}, {"steering_angle", 0.0}, {"power_mode", 0}
        }}
    };
    return templates;
}

uint64_t getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool init_mqtt() {
    auto mqtt_config = g_config->getMQTTConfig("cloud_mqtt_sender");
    mosquitto_lib_init();
    g_mosq = mosquitto_new(mqtt_config.client_id.c_str(), true, nullptr);
    if (!g_mosq) return false;
    
    if (mosquitto_connect(g_mosq, mqtt_config.host.c_str(), mqtt_config.port, mqtt_config.keepalive) != MOSQ_ERR_SUCCESS) 
        return false;
    if (mosquitto_loop_start(g_mosq) != MOSQ_ERR_SUCCESS) return false;
    
    std::cout << "[SUCCESS] MQTT连接成功: " << mqtt_config.host << ":" << mqtt_config.port << std::endl;
    return true;
}

void cleanup() {
    g_running = false;
    g_stream_running = false;
    if (g_mosq) {
        mosquitto_loop_stop(g_mosq, true);
        mosquitto_disconnect(g_mosq);
        mosquitto_destroy(g_mosq);
    }
    mosquitto_lib_cleanup();
    if (g_logger) g_logger->shutdown();
}

std::string normalize_topic(const std::string& topic) {
    if (topic.find('/') != std::string::npos) return topic;
    if (topic == "control_cmd") return "/vehicle/control_cmd";
    if (topic == "handshake_request") return "/handshake/request";
    if (topic == "handshake_response") return "/handshake/response";
    if (topic == "vehicle_status") return "/vehicle/vehicle_status";
    return "/" + topic;
}

std::map<std::string, double> parse_params(int argc, char* argv[], int start_idx) {
    std::map<std::string, double> params;
    for (int i = start_idx; i < argc; i++) {
        std::string arg = argv[i];
        size_t eq_pos = arg.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = arg.substr(0, eq_pos);
            std::string value_str = arg.substr(eq_pos + 1);
            try {
                params[key] = std::stod(value_str);
            } catch (const std::exception&) {
                std::cerr << "警告: 无法解析参数 " << arg << std::endl;
            }
        } else if (i + 1 < argc) {
            // 支持 key value 格式
            try {
                params[arg] = std::stod(argv[i + 1]);
                i++; // 跳过下一个参数
            } catch (const std::exception&) {
                std::cerr << "警告: 无法解析参数 " << arg << " " << argv[i + 1] << std::endl;
            }
        }
    }
    return params;
}

std::string build_json(const std::string& topic, const std::map<std::string, double>& custom_params) {
    auto templates = get_templates();
    std::map<std::string, double> final_params;
    
    // 获取默认模板
    if (templates.find(topic) != templates.end()) {
        final_params = templates[topic];
    }
    
    // 用自定义参数覆盖
    for (const auto& [key, value] : custom_params) {
        final_params[key] = value;
    }
    
    // 构建JSON
    std::ostringstream json;
    json << "{";
    bool first = true;
    for (const auto& [key, value] : final_params) {
        if (!first) json << ",";
        json << "\"" << key << "\":" << value;
        first = false;
    }
    json << ",\"timestamp\":" << getCurrentTimestamp();
    json << ",\"cloud_timestamp\":" << getCurrentTimestamp();
    json << "}";
    
    return json.str();
}

bool send_message(const std::string& topic, const std::map<std::string, double>& params) {
    if (!g_mosq) return false;
    
    std::string json_message = build_json(topic, params);
    int result = mosquitto_publish(g_mosq, nullptr, topic.c_str(), 
                                 json_message.length(), json_message.c_str(), 0, false);
    
    if (result == MOSQ_ERR_SUCCESS) {
        g_message_sequence++;
        std::cout << "[SUCCESS] 发送消息 [" << topic << "] " << json_message << std::endl;
        
        if (g_logger) {
            std::string log_name = topic.substr(1); // 去掉开头的/
            std::replace(log_name.begin(), log_name.end(), '/', '_');
            g_logger->logMessage(log_name, "MQTT发送成功: " + json_message, 
                               getCurrentTimestamp(), g_message_sequence);
        }
        return true;
    }
    
    std::cout << "[ERROR] 发送失败: " << result << std::endl;
    return false;
}

void send_mode(const std::string& topic, const std::map<std::string, double>& params) {
    std::cout << "发送单条消息到: " << topic << std::endl;
    send_message(topic, params);
}

void continuous_mode(const std::string& topic, const std::map<std::string, double>& params) {
    std::cout << "持续100Hz发送: " << topic << " (Ctrl+C停止)" << std::endl;
    
    const int64_t interval_us = 10000; // 10ms = 100Hz
    uint64_t count = 0;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    while (g_running) {
        auto loop_start = std::chrono::high_resolution_clock::now();
        
        if (send_message(topic, params)) count++;
        
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - loop_start).count();
        
        if (interval_us > elapsed) {
            std::this_thread::sleep_for(std::chrono::microseconds(interval_us - elapsed));
        }
        
        // 每5秒显示统计
        if (count % 500 == 0) {
            auto total_time = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::high_resolution_clock::now() - start_time).count();
            double freq = (total_time > 0) ? (double)count / total_time : 0.0;
            std::cout << "[STATS] 已发送: " << count << " 条, 频率: " << freq << "Hz" << std::endl;
        }
    }
    
    std::cout << "发送停止, 总计: " << count << " 条" << std::endl;
}

void stream_mode(const std::string& topic, const std::map<std::string, double>& initial_params) {
    g_stream_topic = topic;
    {
        std::lock_guard<std::mutex> lock(g_stream_mutex);
        g_stream_params = initial_params;
    }
    g_stream_running = true;
    
    std::cout << "=== 流式发送模式 ===\n";
    std::cout << "Topic: " << topic << " (100Hz)" << std::endl;
    std::cout << "初始参数: ";
    for (const auto& [key, value] : initial_params) {
        std::cout << key << "=" << value << " ";
    }
    std::cout << "\n\n";
    
    // 启动发送线程
    std::thread sender([topic]() {
        const int64_t interval_us = 10000; // 100Hz
        uint64_t count = 0;
        
        while (g_stream_running && g_running) {
            auto loop_start = std::chrono::high_resolution_clock::now();
            
            std::map<std::string, double> current_params;
            {
                std::lock_guard<std::mutex> lock(g_stream_mutex);
                current_params = g_stream_params;
            }
            
            if (send_message(topic, current_params)) count++;
            
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - loop_start).count();
            
            if (interval_us > elapsed) {
                std::this_thread::sleep_for(std::chrono::microseconds(interval_us - elapsed));
            }
            
            if (count % 500 == 0) {
                std::cout << "[STATS] 已发送: " << count << " 条" << std::endl;
            }
        }
        
        std::cout << "\n[INFO] 发送线程停止, 总计: " << count << " 条" << std::endl;
    });
    
    // 处理用户输入
    std::cout << "输入新参数更新消息 (格式: key=value 或 key value):\n";
    std::cout << "'show' 查看当前参数, 'quit' 退出\n\n";
    
    std::string line;
    while (g_stream_running && g_running && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        
        if (line == "quit" || line == "exit" || line == "q") {
            break;
        } else if (line == "show") {
            std::lock_guard<std::mutex> lock(g_stream_mutex);
            std::cout << "当前参数: ";
            for (const auto& [key, value] : g_stream_params) {
                std::cout << key << "=" << value << " ";
            }
            std::cout << std::endl;
        } else {
            // 解析输入
            std::istringstream iss(line);
            std::string key, value_str;
            if (line.find('=') != std::string::npos) {
                size_t eq_pos = line.find('=');
                key = line.substr(0, eq_pos);
                value_str = line.substr(eq_pos + 1);
            } else if (iss >> key >> value_str) {
                // key value 格式
            } else {
                std::cout << "格式错误，请使用: key=value 或 key value" << std::endl;
                continue;
            }
            
            try {
                double value = std::stod(value_str);
                {
                    std::lock_guard<std::mutex> lock(g_stream_mutex);
                    g_stream_params[key] = value;
                }
                std::cout << "[UPDATE] " << key << "=" << value << std::endl;
            } catch (const std::exception&) {
                std::cout << "[ERROR] 无法解析值: " << value_str << std::endl;
            }
        }
    }
    
    g_stream_running = false;
    if (sender.joinable()) sender.join();
    std::cout << "[INFO] 流式发送模式结束" << std::endl;
}

void show_usage(const char* prog_name) {
    std::cout << "=== J6云端MQTT发送器(简化版) ===\n\n";
    std::cout << "使用方法:\n";
    std::cout << "  " << prog_name << " send <topic> [key=value...]     # 发送单条消息\n";
    std::cout << "  " << prog_name << " continuous <topic> [key=value...] # 持续100Hz发送\n";
    std::cout << "  " << prog_name << " stream <topic> [key=value...]   # 流式发送，可动态更新\n";
    std::cout << "  " << prog_name << " help                           # 显示帮助\n\n";
    
    std::cout << "topic 可以是:\n";
    std::cout << "  control_cmd, handshake_request, handshake_response, vehicle_status\n";
    std::cout << "  或完整路径如 /vehicle/control_cmd\n\n";
    
    std::cout << "示例:\n";
    std::cout << "  " << prog_name << " send control_cmd steering_angle=15.5 target_acceleration=0.8\n";
    std::cout << "  " << prog_name << " send handshake_request remote_override_status=1\n";
    std::cout << "  " << prog_name << " continuous vehicle_status speed=30.0\n";
    std::cout << "  " << prog_name << " stream control_cmd steering_angle=10.0\n\n";
}

void signal_handler(int signal) {
    std::cout << "\n接收到停止信号，准备退出..." << std::endl;
    g_running = false;
    g_stream_running = false;
}

int main(int argc, char* argv[]) {
    std::cout << "=== J6下行系统 - 云端MQTT发送器(简化版) ===" << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (argc < 2 || std::string(argv[1]) == "help") {
        show_usage(argv[0]);
        return 0;
    }
    
    // 加载配置和初始化
    std::string config_file = "/home/wjj/work/project_root/j6/downlink/config/downlink_config.json";
    g_config = std::make_unique<ConfigManager>();
    if (!g_config->loadConfig(config_file)) {
        std::cerr << "[ERROR] 加载配置文件失败" << std::endl;
        return 1;
    }
    
    g_logger = std::make_unique<AsyncLogger>();
    auto logging_config = g_config->getLoggingConfig();
    if (!g_logger->init(logging_config.log_base_dir)) {
        std::cerr << "[ERROR] 日志系统初始化失败" << std::endl;
        return 1;
    }
    
    if (!init_mqtt()) {
        cleanup();
        return 1;
    }
    
    // 解析命令
    std::string cmd = argv[1];
    
    if (cmd == "send" && argc >= 3) {
        std::string topic = normalize_topic(argv[2]);
        auto params = parse_params(argc, argv, 3);
        send_mode(topic, params);
        
    } else if (cmd == "continuous" && argc >= 3) {
        std::string topic = normalize_topic(argv[2]);
        auto params = parse_params(argc, argv, 3);
        continuous_mode(topic, params);
        
    } else if (cmd == "stream" && argc >= 3) {
        std::string topic = normalize_topic(argv[2]);
        auto params = parse_params(argc, argv, 3);
        stream_mode(topic, params);
        
    } else {
        std::cout << "参数错误" << std::endl;
        show_usage(argv[0]);
        cleanup();
        return 1;
    }
    
    cleanup();
    return 0;
}