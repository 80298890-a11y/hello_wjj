/**
 * @file cloud_mqtt_sender.cpp
 * @brief J6下行系统 - 云端MQTT发送器
 * 
 * 功能：模拟云端发送控制指令到MQTT服务器
 * 发送JSON消息到MQTT，包含车机控制命令
 * 
 * 使用：
 * ./cloud_mqtt_sender                           # 默认100Hz发送control_cmd
 * ./cloud_mqtt_sender continuous <topic>        # 指定topic持续100Hz发送  
 * ./cloud_mqtt_sender custom <topic> <hz> <sec> # 自定义发送模式
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <atomic>
#include <fstream>
#include <map>
#include <vector>
#include <mosquitto.h>
#include "AsyncLogger.h"
#include "ConfigManager.h"

// 全局控制变量
std::atomic<bool> g_running{true};

// MQTT相关全局变量
struct mosquitto* g_mosq = nullptr;
const char* mqtt_host = "hellorobotaxi.cn";
int mqtt_port = 11883;
const char* client_id = "j6_cloud_mqtt_sender";

// 全局日志器和配置管理器
std::unique_ptr<AsyncLogger> g_logger = nullptr;
std::unique_ptr<ConfigManager> g_config = nullptr;
uint64_t g_message_sequence = 0;

bool publish_mqtt_message(const std::string& topic, const std::string& message) {
    if (!g_mosq) return false;
    
    int socket = mosquitto_socket(g_mosq);
    if (socket == -1) {
        if (mosquitto_reconnect(g_mosq) != MOSQ_ERR_SUCCESS) return false;
    }
    
    int result = mosquitto_publish(g_mosq, nullptr, topic.c_str(), 
                                 message.length(), message.c_str(), 0, false);
    if (result != MOSQ_ERR_SUCCESS) return false;
    
    mosquitto_loop_write(g_mosq, 0);
    return true;
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
    
    if (mosquitto_connect(g_mosq, mqtt_config.host.c_str(), mqtt_config.port, mqtt_config.keepalive) != MOSQ_ERR_SUCCESS) return false;
    if (mosquitto_loop_start(g_mosq) != MOSQ_ERR_SUCCESS) return false;
    
    std::cout << "[SUCCESS] MQTT客户端连接成功: " << mqtt_config.host << ":" << mqtt_config.port << std::endl;
    return true;
}

void cleanup_mqtt() {
    if (g_mosq) {
        mosquitto_loop_stop(g_mosq, true);
        mosquitto_disconnect(g_mosq);
        mosquitto_destroy(g_mosq);
        g_mosq = nullptr;
    }
    mosquitto_lib_cleanup();
}

std::string get_topic_log_name(const std::string& topic) {
    if (topic == "/vehicle/control_cmd") return "vehicle_control_cmd";
    if (topic == "/handshake/request") return "handshake_request";
    if (topic == "/handshake/response") return "handshake_response";
    if (topic == "/vehicle/vehicle_status") return "vehicle_vehicle_status";
    if (topic.find("tsp/command/") == 0) {
        std::string command_id = topic.substr(12);
        return "tsp_command_" + command_id;
    }
    return "unknown_" + topic;
}

std::string build_json_message(const std::string& topic, const std::map<std::string, double>& params) {
    std::ostringstream json;
    json << "{";
    
    bool first = true;
    
    // 添加自定义参数
    for (const auto& [key, value] : params) {
        if (!first) json << ",";
        json << "\"" << key << "\":" << value;
        first = false;
    }
    
    // 添加云端时间戳
    if (!first) json << ",";
    json << "\"cloud_timestamp\":" << getCurrentTimestamp();
    json << "}";
    
    return json.str();
}

bool send_single_message(const std::string& topic, const std::map<std::string, double>& params) {
    std::string json_message = build_json_message(topic, params);
    bool result = publish_mqtt_message(topic, json_message);
    
    g_message_sequence++;
    
    if (result) {
        std::string topic_log_name = get_topic_log_name(topic);
        std::string log_content = "MQTT发送成功 [" + topic + "] " + json_message;
        
        if (g_logger) {
            g_logger->logMessage(topic_log_name, log_content, getCurrentTimestamp(), g_message_sequence);
        }
        
        std::cout << "[SUCCESS] " << log_content << std::endl;
    } else {
        std::string log_content = "MQTT发送失败 [" + topic + "]";
        std::cout << "[ERROR] " << log_content << std::endl;
        
        if (g_logger) {
            std::string topic_log_name = get_topic_log_name(topic);
            g_logger->logMessage(topic_log_name, log_content, getCurrentTimestamp(), g_message_sequence);
        }
    }
    
    return result;
}

void precise_sleep_us(int64_t microseconds) {
    auto start = std::chrono::high_resolution_clock::now();
    auto target = start + std::chrono::microseconds(microseconds);
    while (std::chrono::high_resolution_clock::now() < target) {
        std::this_thread::yield();
    }
}

void continuous_single_topic_mode(const std::string& topic, const std::map<std::string, double>& params) {
    std::cout << "持续100Hz发送: " << topic << " (Ctrl+C停止)" << std::endl;
    
    const int64_t TARGET_INTERVAL_US = 10000;  // 10ms = 100Hz
    uint64_t message_count = 0;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    while (g_running) {
        auto loop_start = std::chrono::high_resolution_clock::now();
        
        if (send_single_message(topic, params)) {
            message_count++;
        }
        
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - loop_start).count();
        
        int64_t sleep_time_us = TARGET_INTERVAL_US - elapsed_us;
        if (sleep_time_us > 0) {
            precise_sleep_us(sleep_time_us);
        }
        
        // 每5秒显示统计
        if (message_count % 500 == 0) {
            auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::high_resolution_clock::now() - start_time).count();
            double freq = (elapsed_sec > 0) ? (double)message_count / elapsed_sec : 0.0;
            std::cout << "已发送: " << message_count << " 条, 频率: " 
                     << std::fixed << std::setprecision(1) << freq << "Hz" << std::endl;
        }
    }
    
    std::cout << "发送停止, 总计: " << message_count << " 条" << std::endl;
}

void custom_send_mode(const std::string& topic, int frequency_hz, int duration_sec, const std::map<std::string, double>& params) {
    std::cout << "自定义发送: " << topic << " " << frequency_hz << "Hz " << duration_sec << "秒" << std::endl;
    
    int64_t interval_us = 1000000 / frequency_hz;
    int total_messages = frequency_hz * duration_sec;
    int sent_count = 0;
    
    for (int i = 0; i < total_messages && g_running; i++) {
        auto loop_start = std::chrono::high_resolution_clock::now();
        
        if (send_single_message(topic, params)) {
            sent_count++;
        }
        
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - loop_start).count();
        
        int64_t sleep_time_us = interval_us - elapsed_us;
        if (sleep_time_us > 0) {
            precise_sleep_us(sleep_time_us);
        }
        
        // 每秒显示统计
        if ((i + 1) % frequency_hz == 0) {
            std::cout << "已发送: " << sent_count << " 条" << std::endl;
        }
    }
    
    std::cout << "发送完成, 总计: " << sent_count << " 条" << std::endl;
}

std::string normalize_topic(const std::string& topic) {
    if (topic.find('/') != std::string::npos) return topic;
    if (topic == "control_cmd") return "/vehicle/control_cmd";
    if (topic == "handshake_request") return "/handshake/request";
    if (topic == "handshake_response") return "/handshake/response";
    if (topic == "vehicle_status") return "/vehicle/vehicle_status";
    return "/" + topic;
}

void show_usage(const char* prog_name) {
    std::cout << "J6云端MQTT发送器使用说明:\n\n";
    std::cout << "1. 默认模式: " << prog_name << " (默认100Hz发送control_cmd)\n";
    std::cout << "2. 持续100Hz: " << prog_name << " continuous <topic> [params...]\n";
    std::cout << "3. 自定义: " << prog_name << " custom <topic> <hz> <sec> [params...]\n\n";
    std::cout << "示例:\n";
    std::cout << "  " << prog_name << "  # 默认100Hz发送control_cmd\n";
    std::cout << "  " << prog_name << " continuous control_cmd steering_angle=15.5\n";
    std::cout << "  " << prog_name << " custom control_cmd 50 10 throttle=0.8\n";
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
        }
    }
    
    return params;
}

void signal_handler(int signal) {
    static int signal_count = 0;
    signal_count++;
    
    if (signal_count == 1) {
        std::cout << "\n接收到停止信号，准备退出..." << std::endl;
        g_running = false;
    } else {
        std::cout << "\n强制退出..." << std::endl;
        cleanup_mqtt();
        if (g_logger) {
            g_logger->shutdown();
            g_logger.reset();
        }
        exit(1);
    }
}

void cleanup() {
    cleanup_mqtt();
    if (g_logger) {
        g_logger->shutdown();
        g_logger.reset();
    }
    std::cout << "清理完成" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "=== J6下行系统 - 云端MQTT发送器 ===" << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 加载配置文件
    std::string config_file = (argc > 1) ? argv[1] : "/home/wjj/work/project_root/j6/downlink/config/downlink_config.json";
    
    g_config = std::make_unique<ConfigManager>();
    if (!g_config->loadConfig(config_file)) {
        std::cerr << "[ERROR] 加载配置文件失败: " << config_file << std::endl;
        return 1;
    }
    
    g_config->printConfig();

    // 初始化异步日志系统
    g_logger = std::make_unique<AsyncLogger>();
    auto logging_config = g_config->getLoggingConfig();
    if (!g_logger->init(logging_config.log_base_dir)) {
        std::cerr << "[ERROR] 异步日志系统初始化失败!" << std::endl;
        return 1;
    }

    if (!init_mqtt()) {
        cleanup();
        return 1;
    }

    if (argc == 1 || (argc == 2 && argv[1] == config_file)) {
        // 默认模式：使用配置文件的参数
        std::string default_topic = g_config->getString("cloud_mqtt_sender.default_topic", "/vehicle/control_cmd");
        int default_freq = g_config->getInt("cloud_mqtt_sender.default_frequency_hz", 100);
        
        std::cout << "默认模式: " << default_freq << "Hz发送 " << default_topic << std::endl;
        
        // 从配置文件获取默认参数
        std::map<std::string, double> default_params = g_config->getParamsMap("cloud_mqtt_sender.default_params");
        if (default_params.empty()) {
            // 如果配置文件没有参数，使用硬编码默认值 - 所有字段都为0
            default_params = {
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
                {"ebrake_status", 0.0}
            };
        }
        
        continuous_single_topic_mode(default_topic, default_params);
        
    } else if (argc >= 3 && std::string(argv[1]) == "continuous") {
        // 指定topic持续100Hz发送
        std::string topic = normalize_topic(argv[2]);
        std::map<std::string, double> params = parse_params(argc, argv, 3);
        continuous_single_topic_mode(topic, params);
        
    } else if (argc >= 5 && std::string(argv[1]) == "custom") {
        // 自定义发送模式
        std::string topic = normalize_topic(argv[2]);
        int frequency_hz = std::atoi(argv[3]);
        int duration_sec = std::atoi(argv[4]);
        
        if (frequency_hz < 1 || frequency_hz > 1000 || duration_sec < 1 || duration_sec > 3600) {
            std::cerr << "参数错误: 频率1-1000Hz, 时间1-3600秒" << std::endl;
            cleanup();
            return 1;
        }
        
        std::map<std::string, double> params = parse_params(argc, argv, 5);
        custom_send_mode(topic, frequency_hz, duration_sec, params);
        
    } else if (argc >= 2 && (std::string(argv[1]) == "help" || std::string(argv[1]) == "-h")) {
        show_usage(argv[0]);
    } else {
        std::cout << "参数错误" << std::endl;
        show_usage(argv[0]);
        cleanup();
        return 1;
    }

    cleanup();
    return 0;
}
