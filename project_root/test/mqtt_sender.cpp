#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <map>
#include <algorithm>
#include <signal.h>
#include <sstream>
#include <iomanip>
#include <mosquitto.h>
#include "logger_conf/universal_logger.h"

volatile bool g_running = true;

void signal_handler(int signal) {
    static int signal_count = 0;
    if (++signal_count == 1) {
        std::cout << "\n[INFO] 收到退出信号，正在清理..." << std::endl;
        g_running = false;
    } else {
        std::cout << "\n[INFO] 强制退出..." << std::endl;
        _Exit(1);
    }
}


std::string get_topic_log_path(const std::string& topic) {
    if (topic.find("/handshake/request") == 0) return "handshake/request";
    if (topic.find("/vehicle/control_cmd") == 0) return "vehicle/control_cmd";
    std::string path = topic;
    if (path.front() == '/') path = path.substr(1);
    std::replace(path.begin(), path.end(), '/', '_');
    return path;
}

struct mosquitto* g_mosq = nullptr;
    const char* mqtt_host = "hellorobotaxi.cn";
// const char* mqtt_host = "192.168.2.90";
int mqtt_port = 11883;
const char* client_id = "mqtt_sender_8975";
std::string g_vehicle_id = "12345678";
std::map<std::string, std::map<std::string, double>> g_last_params;

std::string addVIDToTopic(const std::string& topic);
bool publish_mqtt_message(const std::string& topic, const std::string& message, bool retain = false) {
    if (!g_mosq) return false;
    if (mosquitto_socket(g_mosq) == -1 && mosquitto_reconnect(g_mosq) != MOSQ_ERR_SUCCESS) return false;
    int result = mosquitto_publish(g_mosq, nullptr, topic.c_str(), message.length(), message.c_str(), 2, retain);
    if (result == MOSQ_ERR_SUCCESS) mosquitto_loop_write(g_mosq, 0);
    return result == MOSQ_ERR_SUCCESS;
}

uint64_t getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool init_mqtt() {
    mosquitto_lib_init();
    g_mosq = mosquitto_new(client_id, true, nullptr);
    if (!g_mosq || mosquitto_connect(g_mosq, mqtt_host, mqtt_port, 60) != MOSQ_ERR_SUCCESS ||
        mosquitto_loop_start(g_mosq) != MOSQ_ERR_SUCCESS) return false;
    std::cout << "[SUCCESS] MQTT客户端连接成功" << std::endl;
    return true;
}

void cleanup_mqtt() {
    if (g_mosq) {
        mosquitto_loop_stop(g_mosq, true);
        mosquitto_destroy(g_mosq);
        g_mosq = nullptr;
    }
    mosquitto_lib_cleanup();
    std::cout << "[INFO] MQTT客户端已清理" << std::endl;
}
std::string build_json_message(const std::string& topic, const std::map<std::string, double>& params) {
    std::ostringstream json;
    json << "{";
    bool first = true;
    
    for (const auto& [key, value] : params) {
        if (!first) json << ",";
        if (key == "timestamp") {
            json << "\"" << key << "\":" << getCurrentTimestamp();
        } else if (key == "vehicle_instruction_takeover_timestamp") {
            // 保留原始时间戳，转换为整数
            json << "\"" << key << "\":" << static_cast<uint64_t>(value);
        } else {
            json << "\"" << key << "\":" << value;
        }
        first = false;
    }
    
    if (!first) json << ",";
    json << "\"mqtt_sender_timestamp\":" << getCurrentTimestamp() << "}";
    return json.str();
}
bool send_single_message(const std::string& topic, const std::map<std::string, double>& params, bool retain = false, bool verbose = true) {
    std::string actual_topic = addVIDToTopic(topic);
    std::string json_message = build_json_message(actual_topic, params);
    bool result = publish_mqtt_message(actual_topic, json_message, retain);
    
    if (result) {
        std::string log_content = "MQTT发送成功 [" + actual_topic + "] " + json_message;
        LOG_BY_TOPIC(get_topic_log_path(topic), log_content);
        if (verbose) std::cout << "[SUCCESS] " << log_content << std::endl;
    } else if (verbose) {
        std::cout << "[ERROR] MQTT发送失败 [" << actual_topic << "]" << std::endl;
    }
    
    return result;
}

bool send_three_frames(const std::string& topic, const std::map<std::string, double>& params, bool use_retain) {
    int success_count = 0;
    std::ostringstream updated_json;
    updated_json << "{";
    bool first = true;
    
    for (const auto& [key, value] : params) {
        bool is_meaningful = (key.find("_enable") != std::string::npos && value == 1) || 
                           (key.find("_enable") == std::string::npos && value != 0);
        if (is_meaningful) {
            if (!first) updated_json << ",";
            updated_json << "\"" << key << "\":" << std::fixed << std::setprecision(1) << value;
            first = false;
        }
    }
    updated_json << "}";
    
    for (int i = 1; i <= 3; i++) {
        if (!g_running) break;
        if (send_single_message(topic, params, use_retain, false)) success_count++;
        if (i < 3) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    std::cout << "[" << addVIDToTopic(topic) << "]" << updated_json.str() << std::endl;
    return success_count > 0;
}

std::string readVIDFromConfig() {
    std::ifstream file("../car_config/vehicle_config.yaml");
    if (!file.is_open()) return "10001001";
    
    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find("vid:");
        if (pos != std::string::npos) {
            size_t quote_start = line.find("\"", pos);
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

std::string addVIDToTopic(const std::string& topic) {
    std::string vid = readVIDFromConfig();
    std::string vid_suffix = "/" + vid;
    if (topic.length() >= vid_suffix.length() && 
        topic.substr(topic.length() - vid_suffix.length()) == vid_suffix) {
        return topic;
    }
    return topic + vid_suffix;
}

std::string normalize_topic(const std::string& topic) {
    if (topic.find('/') != std::string::npos) return topic;
    static std::map<std::string, std::string> topics = {
        {"handshake_request", "/handshake/request"},
        {"control_cmd", "/vehicle/control_cmd"}
    };
    auto it = topics.find(topic);
    std::string base_topic = it != topics.end() ? it->second : "/" + topic;
    
    if (base_topic == "/handshake/request" || base_topic == "/vehicle/control_cmd") {
        return base_topic + "/" + g_vehicle_id;
    }
    return base_topic;
}

bool parse_retain_flag(int argc, char* argv[], int start_idx) {
    for (int i = start_idx; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--retain" || arg == "retain" || arg == "retain=true") return true;
        if (arg == "retain=false") return false;
        size_t eq = arg.find('=');
        if (eq != std::string::npos && arg.substr(0, eq) == "retain") {
            std::string value = arg.substr(eq + 1);
            return (value == "1" || value == "true");
        }
    }
    return true;
}

std::map<std::string, double> parse_params(int argc, char* argv[], int start) {
    std::map<std::string, double> params;
    for (int i = start; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--") == 0 || arg.find("retain") == 0) continue;
        size_t eq = arg.find('=');
        if (eq != std::string::npos) {
            try { params[arg.substr(0, eq)] = std::stod(arg.substr(eq + 1)); }
            catch (...) {}
        }
    }
    return params;
}
int main(int argc, char* argv[]) {
    if (argc == 2 && (std::string(argv[1]) == "help" || std::string(argv[1]) == "--help")) {
        std::cout << "用法: " << argv[0] << " <topic> [key=val...] [--continuous]" << std::endl;
        std::cout << "单次: 发送3帧后退出 | 连续: 等待输入，保持连接" << std::endl;
        return 0;
    }
    
    if (argc < 2) {
        std::cerr << "错误: 请指定topic\n使用 '" << argv[0] << " help' 查看使用说明" << std::endl;
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    bool continuous_mode = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--continuous") {
            continuous_mode = true;
            break;
        }
    }
    
    g_vehicle_id = readVIDFromConfig();
    initUniversalLogger("mqtt_sender");
    if (!init_mqtt()) { cleanup_mqtt(); return 1; }
    
    std::string user_topic = argv[1];
    std::string full_topic = normalize_topic(user_topic);
    std::map<std::string, double> params;
    bool use_retain = true;
    
    if (!continuous_mode) {
        params = parse_params(argc, argv, 2);
        use_retain = parse_retain_flag(argc, argv, 2);
    } else {
        use_retain = parse_retain_flag(argc, argv, 2);
    }
    
    std::string base_topic = (user_topic == "handshake_request") ? "/handshake/request" :
                           (user_topic == "control_cmd") ? "/vehicle/control_cmd" : full_topic;
    
    std::map<std::string, double> final_params = !params.empty() ? params : 
        (g_last_params.find(base_topic) != g_last_params.end() ? g_last_params[base_topic] : 
         std::map<std::string, double>{});
    
    g_last_params[base_topic] = final_params;
    
    if (continuous_mode) {
        std::string input;
        while (g_running && std::getline(std::cin, input)) {
            input.erase(0, input.find_first_not_of(" \t"));
            input.erase(input.find_last_not_of(" \t") + 1);
            if (input.empty() || input == "quit") break;
            
            std::map<std::string, double> current_params = final_params;
            std::istringstream iss(input);
            std::string param;
            while (iss >> param) {
                size_t pos = param.find('=');
                if (pos != std::string::npos) {
                    try { current_params[param.substr(0, pos)] = std::stod(param.substr(pos + 1)); }
                    catch (...) {}
                }
            }
            
            if (send_three_frames(full_topic, current_params, use_retain)) {
                g_last_params[base_topic] = current_params;
                final_params = current_params;
            }
        }
    } else {
        send_three_frames(full_topic, final_params, use_retain);
    }
    
    cleanup_mqtt(); 
    cleanupUniversalLogger();
    return 0;
}