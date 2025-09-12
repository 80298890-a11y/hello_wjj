/**
 * MQTT发送器 - 精简版
 * 用法: ./mqtt_sender <topic> [params...] [--continuous]
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>
#include <algorithm>
#include <signal.h>
#include <sstream>
#include <iomanip>
#include <mosquitto.h>
#include "logger_conf/universal_logger.h"

// 全局控制变量
volatile bool g_running = true;

/**
 * @brief 信号处理函数
 */
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n[INFO] 收到退出信号，正在清理..." << std::endl;
        g_running = false;
    }
}


// 获取topic日志路径
std::string get_topic_log_path(const std::string& topic) {
    if (topic.find("/handshake/request") == 0) return "handshake/request";
    if (topic.find("/vehicle/control_cmd") == 0) return "vehicle/control_cmd";
    if (topic.find("tsp/command") == 0) return "tsp/command";
    // 默认路径
    std::string path = topic;
    if (path.front() == '/') path = path.substr(1);
    std::replace(path.begin(), path.end(), '/', '_');
    return path;
}

// 函数声明
std::string addVIDToTopic(const std::string& topic);

// MQTT相关全局变量
struct mosquitto* g_mosq = nullptr;
const char* mqtt_host = "hellorobotaxi.cn";
int mqtt_port = 11883;
const char* client_id = "mqtt_sender";

// VID配置
std::string g_vehicle_id = "12345678"; // 默认VID，从配置文件读取

// 状态保持：记录每个topic的上一次参数值
std::map<std::string, std::map<std::string, double>> g_last_params;

// 状态文件路径
const std::string STATE_FILE = "logs/mqtt/.mqtt_sender_state.json";

// 简化的状态管理
void load_last_params() {}
void save_last_params() {}

// 获取topic的默认参数定义
std::map<std::string, double> getDefaultParams(const std::string& topic) {
    if (topic == "/handshake/request") {
        return {{"noa_active_request", 0.0}, {"remote_override_status", 0.0}, {"remote_override_ready", 0.0}};
    }
    if (topic == "/vehicle/control_cmd") {
        return {{"steering_angle_enable", 0.0}, {"steering_angle", 0.0}, {"target_acceleration_enable", 0.0}, 
                {"target_acceleration", 0.0}, {"indicator_left_enable", 0.0}, {"indicator_left", 0.0}, 
                {"indicator_right_enable", 0.0}, {"indicator_right", 0.0}, {"gear_position_enable", 0.0}, 
                {"gear_position", 0.0}, {"ebrake_status_enable", 0.0}, {"ebrake_status", 0.0}, 
                {"lane_change", 0.0}, {"timestamp", 0.0}};
    }
    return {};
}
 bool publish_mqtt_message(const std::string& topic, const std::string& message, bool retain = false) {
    if (!g_mosq) return false;
    
    int socket = mosquitto_socket(g_mosq);
    if (socket == -1) {
        if (mosquitto_reconnect(g_mosq) != MOSQ_ERR_SUCCESS) return false;
    }
    
    int result = mosquitto_publish(g_mosq, nullptr, topic.c_str(), 
                                 message.length(), message.c_str(), 0, retain);
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
        // 停止循环线程并等待
        mosquitto_loop_stop(g_mosq, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 等待线程结束
        
        // 断开连接
        mosquitto_disconnect(g_mosq);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // 等待断开完成
        
        // 销毁客户端
        mosquitto_destroy(g_mosq);
        g_mosq = nullptr;
    }
    mosquitto_lib_cleanup();
    std::cout << "[INFO] MQTT客户端已清理" << std::endl;
}
 // 日志相关函数移除，使用通用日志系统
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
            // timestamp字段使用当前时间戳，不使用保存的旧值
            if (key == "timestamp") {
                json << "\"" << key << "\":" << getCurrentTimestamp();
            } else {
                json << "\"" << key << "\":" << value;
            }
            first = false;
        }
    }
    
    if (!first) json << ",";
    json << "\"mqtt_send_timestamp\":" << getCurrentTimestamp();
    json << "}";
    
    return json.str();
}
 bool send_single_message(const std::string& topic, const std::map<std::string, double>& params, bool retain = false, bool verbose = true) {
    // 🎯 自动为需要VID的topic添加VID后缀
    std::string actual_topic = addVIDToTopic(topic);
    
    std::string json_message = build_json_message(actual_topic, params);
    bool result = publish_mqtt_message(actual_topic, json_message, retain);
    
    if (result) {
        std::string topic_path = get_topic_log_path(topic); // 日志路径使用原始topic
        std::string log_content = "MQTT发送成功 [" + actual_topic + "] " + json_message;
        LOG_BY_TOPIC(get_topic_log_path(topic), log_content);
        if (verbose) {
            std::cout << "[SUCCESS] " << log_content << std::endl;
        }
    } else {
        if (verbose) {
            std::cout << "[ERROR] MQTT发送失败 [" << actual_topic << "]" << std::endl;
        }
    }
    
    return result;
}

/**
 * @brief 发送3帧，5ms间隔 (显示更新字段JSON)
 */
bool send_three_frames(const std::string& topic, const std::map<std::string, double>& params, bool use_retain) {
    int success_count = 0;
    
    // 生成更新字段的JSON（只显示实际更新的字段）
    std::string updated_json = "{";
    bool first = true;
    for (const auto& [key, value] : params) {
        // 只显示实际设置的非零值，排除默认的enable字段
        bool is_meaningful = false;
        if (key.find("_enable") != std::string::npos && value == 1) {
            is_meaningful = true; // 启用字段且值为1
        } else if (key.find("_enable") == std::string::npos && value != 0) {
            is_meaningful = true; // 非enable字段且非零值
        }
        
        if (is_meaningful) {
            if (!first) updated_json += ",";
            // 格式化数值为1位小数
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << value;
            updated_json += "\"" + key + "\":" + oss.str();
            first = false;
        }
    }
    updated_json += "}";
    
    for (int i = 1; i <= 3; i++) {
        if (!g_running) break;
        
        // 静默发送，不打印详细日志
        if (send_single_message(topic, params, use_retain, false)) {
            success_count++;
        }
        
        // 间隔5ms (最后一帧不需要等待)
        if (i < 3) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    
    // 显示topic和更新字段JSON
    std::string actual_topic = addVIDToTopic(topic);
    std::cout << "[" << actual_topic << "]" << updated_json << std::endl;
    
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

/**
 * @brief 所有topic发送时都添加VID后缀（防止重复添加）
 */
std::string addVIDToTopic(const std::string& topic) {
    // 读取VID配置
    std::string vid = readVIDFromConfig();
    
    // 检查是否已经包含VID，避免重复添加
    std::string vid_suffix = "/" + vid;
    if (topic.length() >= vid_suffix.length() && 
        topic.substr(topic.length() - vid_suffix.length()) == vid_suffix) {
        // 已经包含VID，直接返回
        return topic;
    }
    
    // 没有VID，添加VID后缀
    return topic + vid_suffix;
}

std::string normalize_topic(const std::string& topic) {
    if (topic.find('/') != std::string::npos) return topic;
    static std::map<std::string, std::string> topics = {
        {"handshake_request", "/handshake/request"},
        {"control_cmd", "/vehicle/control_cmd"}, {"tsp_command", "tsp/command/sender_test"}
    };
    auto it = topics.find(topic);
    std::string base_topic = it != topics.end() ? it->second : "/" + topic;
    
    // 对于云端控制topic，添加VID后缀实现指定车辆发送
    if (base_topic == "/handshake/request" || base_topic == "/vehicle/control_cmd") {
        return base_topic + "/" + g_vehicle_id;
    }
    
    return base_topic;
}

// 解析retain参数
bool parse_retain_flag(int argc, char* argv[], int start_idx) {
    for (int i = start_idx; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--retain" || arg == "retain" || arg == "retain=true") {
            return true;
        }
        if (arg == "retain=false") {
            return false;
        }
        // 也支持retain=1/0的格式
        size_t eq = arg.find('=');
        if (eq != std::string::npos && arg.substr(0, eq) == "retain") {
            std::string value = arg.substr(eq + 1);
            return (value == "1" || value == "true");
        }
    }
    return true; // 默认启用retain
}

// 解析参数
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
    // 帮助信息
    if (argc == 2 && (std::string(argv[1]) == "help" || std::string(argv[1]) == "--help")) {
        std::cout << "用法: " << argv[0] << " <topic> [key=val...] [--continuous]" << std::endl;
        std::cout << "单次: 发送3帧后退出 | 连续: 等待输入，保持连接" << std::endl;
        return 0;
    }
    
    // 检查参数
    if (argc < 2) {
        std::cerr << "错误: 请指定topic" << std::endl;
        std::cerr << "使用 '" << argv[0] << " help' 查看使用说明" << std::endl;
        return 1;
    }
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 检查是否为连续模式
    bool continuous_mode = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--continuous") {
            continuous_mode = true;
            break;
        }
    }
    
    // 初始化
    g_vehicle_id = readVIDFromConfig();
    initUniversalLogger("mqtt_sender");
    if (!init_mqtt()) { cleanup_mqtt(); return 1; }
    
    // 解析topic和参数
    std::string user_topic = argv[1];
    std::string full_topic = normalize_topic(user_topic);  // 带VID的完整topic
    
    // 跳过--continuous参数进行解析
    std::map<std::string, double> params;
    bool use_retain = true;  // 默认启用retain
    
    if (!continuous_mode) {
        // 单次模式：正常解析参数
        params = parse_params(argc, argv, 2);
        use_retain = parse_retain_flag(argc, argv, 2);
    } else {
        // 连续模式：解析除--continuous外的参数
        use_retain = parse_retain_flag(argc, argv, 2);
    }
    
    // 获取原始topic用于状态管理（不带VID）
    std::string base_topic;
    if (user_topic == "handshake_request") {
        base_topic = "/handshake/request";
    } else if (user_topic == "control_cmd") {
        base_topic = "/vehicle/control_cmd";
    } else {
        base_topic = full_topic;  // 其他topic保持原样
    }
    
    // 合并默认参数（实现lastvalue功能，使用原始topic作为key）
    load_last_params();
    std::map<std::string, double> default_params = getDefaultParams(base_topic);
    if (g_last_params.find(base_topic) != g_last_params.end()) {
        // 使用上次的参数作为基础
        for (const auto& [key, value] : g_last_params[base_topic]) {
            default_params[key] = value;
        }
    }
    // 用新参数覆盖
    for (const auto& [key, value] : params) {
        default_params[key] = value;
    }
    
    // 更新状态到全局变量，准备保存（使用原始topic作为key）
    g_last_params[base_topic] = default_params;
    
    if (continuous_mode) {
        // 连续模式
        std::string input;
        int count = 0;
        while (g_running && std::getline(std::cin, input)) {
            input.erase(0, input.find_first_not_of(" \t"));
            input.erase(input.find_last_not_of(" \t") + 1);
            if (input.empty() || input == "quit") break;
            
            count++;
            std::map<std::string, double> params = default_params;
            std::istringstream iss(input);
            std::string param;
            while (iss >> param) {
                size_t pos = param.find('=');
                if (pos != std::string::npos) {
                    try { params[param.substr(0, pos)] = std::stod(param.substr(pos + 1)); }
                    catch (...) {}
                }
            }
            
            if (send_three_frames(full_topic, params, use_retain)) {
                g_last_params[base_topic] = params;
                default_params = params;
            }
        }
    } else {
        // 单次模式
        send_three_frames(full_topic, default_params, use_retain);
    }
    
    // 保存状态
    save_last_params();
    
    cleanup_mqtt(); 
    cleanupUniversalLogger();
    return 0;
}
 
 