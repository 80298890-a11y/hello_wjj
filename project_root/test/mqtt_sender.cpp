/**
 * @file mqtt_sender.cpp
 * @brief MQTT消息发送器 - 精简版
 * 
 * 功能：发送JSON消息到MQTT服务器，支持默认100Hz发送和自定义发送模式
 * 
 * 使用：
 * ./mqtt_sender                                    # 默认100Hz发送handshake_request
 * ./mqtt_sender continuous <topic> [params...]    # 指定topic持续100Hz发送
 * ./mqtt_sender custom <topic> <hz> <sec> [params...] # 自定义发送模式
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
 
 // 全局控制变量
 std::atomic<bool> g_running{true};
 
 // MQTT相关全局变量
 struct mosquitto* g_mosq = nullptr;
 const char* mqtt_host = "hellorobotaxi.cn";
 int mqtt_port = 11883;
 const char* client_id = "mqtt_sender";
 
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
    mosquitto_lib_init();
    g_mosq = mosquitto_new(client_id, true, nullptr);
    if (!g_mosq) return false;
    
    if (mosquitto_connect(g_mosq, mqtt_host, mqtt_port, 60) != MOSQ_ERR_SUCCESS) return false;
    if (mosquitto_loop_start(g_mosq) != MOSQ_ERR_SUCCESS) return false;
    
    std::cout << "[SUCCESS] MQTT客户端连接成功" << std::endl;
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
 
 // 简化日志系统
std::string g_log_dir;

void write_log(const std::string& topic, const std::string& content) {
    std::string log_file = g_log_dir + "/" + topic + ".log";
    std::filesystem::create_directories(std::filesystem::path(log_file).parent_path());
    
    std::ofstream file(log_file, std::ios::app);
    if (file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        file << "[" << std::put_time(std::localtime(&time_t), "%H:%M:%S") << "] " 
             << content << std::endl;
    }
}
 
 std::string create_log_directory() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    std::string log_dir = "logs/mqtt/send/" + oss.str();
    std::filesystem::create_directories(log_dir);
    return log_dir;
}

std::string get_topic_log_path(const std::string& topic) {
    if (topic == "/handshake/request") return "handshake/request";
    if (topic == "/handshake/response") return "handshake/response";
    if (topic == "/vehicle/vehicle_status") return "vehicle/vehicle_status";
    if (topic == "/vehicle/control_cmd") return "vehicle/control_cmd";
    if (topic.find("tsp/command/") == 0) {
        std::string command_id = topic.substr(12);
        return "tsp_command/" + command_id;
    }
    return "unknown/" + topic;
}
 
 std::string build_json_message(const std::string& topic, const std::map<std::string, double>& params) {
    std::ostringstream json;
    json << "{";
    
    bool first = true;
    
    // TSP主题特殊处理 - 使用字符串格式
    if (topic.find("tsp/command/") == 0) {
        for (const auto& [key, value] : params) {
            if (!first) json << ",";
            json << "\"" << key << "\":";
            
            // TSP主题的特定字段使用字符串格式
            if (key == "action") {
                json << "\"start\"";  // action字段固定为"start"，模拟车端数据
            } else if (key == "module") {
                json << "\"noa\"";    // module字段固定为"noa"
            } else {
                json << "\"" << value << "\"";  // 其他字段转为字符串
            }
            first = false;
        }
        
        // 添加云端时间戳
        if (!first) json << ",";
        json << "\"timestamp\":" << getCurrentTimestamp();
        first = false;
    } else {
        // 其他主题使用数字格式
        for (const auto& [key, value] : params) {
            if (!first) json << ",";
            json << "\"" << key << "\":" << value;
            first = false;
        }
    }
    
    if (!first) json << ",";
    json << "\"mqtt_send_timestamp\":" << getCurrentTimestamp();
    json << "}";
    
    return json.str();
}
 
 bool send_single_message(const std::string& topic, const std::map<std::string, double>& params) {
    std::string json_message = build_json_message(topic, params);
    bool result = publish_mqtt_message(topic, json_message);
    
    if (result) {
        std::string topic_path = get_topic_log_path(topic);
        std::string log_content = "MQTT发送成功 [" + topic + "] " + json_message;
        write_log(topic_path, log_content);
        std::cout << "[SUCCESS] " << log_content << std::endl;
    } else {
        std::cout << "[ERROR] MQTT发送失败 [" << topic << "]" << std::endl;
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
    if (topic == "handshake_request") return "/handshake/request";
    if (topic == "handshake_response") return "/handshake/response";
    if (topic == "vehicle_status") return "/vehicle/vehicle_status";
    if (topic == "control_cmd") return "/vehicle/control_cmd";
    if (topic == "tsp_command") return "tsp/command/sender_test";
    return "/" + topic;
}

void show_usage(const char* prog_name) {
    std::cout << "MQTT消息发送器使用说明:\n\n";
    std::cout << "1. 默认模式: " << prog_name << " (五个topic轮流发送，总共500Hz)\n";
    std::cout << "2. 持续100Hz: " << prog_name << " continuous <topic> [params...]\n";
    std::cout << "3. 自定义: " << prog_name << " custom <topic> <hz> <sec> [params...]\n\n";
    std::cout << "示例:\n";
    std::cout << "  " << prog_name << "  # 默认五topic轮流发送\n";
    std::cout << "  " << prog_name << " continuous handshake_request noa_active=1\n";
    std::cout << "  " << prog_name << " custom vehicle_status 50 10 speed=60.5\n";
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
        exit(1);
    }
}

void cleanup() {
    cleanup_mqtt();
    std::cout << "清理完成" << std::endl;
}
 
 int main(int argc, char* argv[]) {
    std::cout << "MQTT消息发送器 (hellorobotaxi.cn:11883)" << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    g_log_dir = create_log_directory();
    std::cout << "日志目录: " << g_log_dir << std::endl;

    if (!init_mqtt()) {
        cleanup();
        return 1;
    }

    if (argc >= 2 && (std::string(argv[1]) == "vid123" || std::string(argv[1]) == "vid456")) {
        // VID模式：模拟车端发送TSP命令到指定VID
        std::string vid = std::string(argv[1]).substr(3); // 去掉"vid"前缀
        std::string tsp_topic = "tsp/command/" + vid;
        
        std::cout << "VID模式: 模拟车端发送TSP命令到 " << tsp_topic << " (固定action=start)" << std::endl;
        
        std::map<std::string, double> tsp_params = {
            {"action", 1.0}, {"module", 2.0}
        };
        
        int message_count = 0;
        const int64_t TARGET_INTERVAL_MS = 1000; // 1秒间隔
        
        while (g_running) {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            bool success = send_single_message(tsp_topic, tsp_params);
            message_count++;
            
            if (message_count % 10 == 0) {
                std::cout << "📊 已发送 " << message_count << " 条TSP命令到VID-" << vid << std::endl;
            }
            
            // 等待下一次发送
            auto end_time = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            if (elapsed.count() < TARGET_INTERVAL_MS) {
                std::this_thread::sleep_for(std::chrono::milliseconds(TARGET_INTERVAL_MS - elapsed.count()));
            }
        }
        
    } else if (argc == 1) {
        // 默认模式：五个topic轮流发送，总共500Hz，每个topic 100Hz
        std::cout << "默认模式: 五个topic轮流发送 (总共500Hz，每个topic 100Hz)" << std::endl;
        
        // 定义五个topic和对应的默认参数
        std::vector<std::pair<std::string, std::map<std::string, double>>> topics = {
            {"/handshake/request", {{"noa_active", 1.0}, {"override_status", 0.0}, {"override_ready", 1.0}}},
            {"/handshake/response", {{"noa_active", 1.0}, {"override_status", 0.0}, {"override_ready", 1.0}}},
            {"/vehicle/vehicle_status", {{"vehicle_id", 12345.0}, {"speed", 60.5}, {"battery", 85.0}}},
            {"/vehicle/control_cmd", {{"steering_angle", 15.5}, {"throttle", 0.3}, {"brake", 0.0}}},
            {"tsp/command/sender_test", {{"action", 1.0}, {"module", 2.0}}}
        };
        
        const int64_t TARGET_INTERVAL_US = 2000;  // 2ms = 500Hz总频率
        uint64_t message_count = 0;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        while (g_running) {
            auto loop_start = std::chrono::high_resolution_clock::now();
            
            // 轮流发送五个topic
            int topic_index = message_count % 5;
            const auto& [topic, params] = topics[topic_index];
            
            if (send_single_message(topic, params)) {
                message_count++;
            }
            
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - loop_start).count();
            
            int64_t sleep_time_us = TARGET_INTERVAL_US - elapsed_us;
            if (sleep_time_us > 0) {
                precise_sleep_us(sleep_time_us);
            }
            
            // 每2500条消息显示统计 (2500/500Hz = 5秒)
            if (message_count % 2500 == 0) {
                auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::high_resolution_clock::now() - start_time).count();
                double total_freq = (elapsed_sec > 0) ? (double)message_count / elapsed_sec : 0.0;
                double per_topic_freq = total_freq / 5.0;
                std::cout << "已发送: " << message_count << " 条, 总频率: " 
                         << std::fixed << std::setprecision(1) << total_freq << "Hz, 每topic: " 
                         << per_topic_freq << "Hz" << std::endl;
            }
        }
        
        std::cout << "发送停止, 总计: " << message_count << " 条" << std::endl;
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
 