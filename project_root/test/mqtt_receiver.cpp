/**
 * @file mqtt_receiver.cpp
 * @brief MQTT消息接收业务逻辑程序
 * 
 * 功能:
 * - 多线程接收MQTT消息
 * - 每个topic独立线程
 * - 异步日志，按topic分类存储
 * - 避免消息混乱和接收阻塞
 * 
 * 使用方式：
 * ./mqtt_receiver
 */

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <map>
#include <algorithm>
#include <yaml-cpp/yaml.h>
#include "logger_conf/universal_logger.h"

/**
 * @brief 获取topic对应的日志路径（用于按topic分类日志）
 */
std::string get_topic_log_path(const std::string& topic_name) {
    // 根据topic转换为文件路径
    if (topic_name == "/handshake/request") return "handshake/request";
    if (topic_name == "/handshake/response") return "handshake/response";
    if (topic_name == "/vehicle/control_cmd") return "vehicle/control_cmd";
    if (topic_name == "/vehicle/vehicle_status") return "vehicle/vehicle_status";
    if (topic_name == "/vehicle/drive_state") return "vehicle/drive_state";
    if (topic_name == "/vehicle/fault_report") return "vehicle/fault_report";
    
    // 处理带VID的topic
    if (topic_name.find("/handshake/request/") == 0) return "handshake/request";
    if (topic_name.find("/handshake/response/") == 0) return "handshake/response";
    if (topic_name.find("/vehicle/control_cmd/") == 0) return "vehicle/control_cmd";
    if (topic_name.find("/vehicle/vehicle_status/") == 0) return "vehicle/vehicle_status";
    if (topic_name.find("/vehicle/drive_state/") == 0) return "vehicle/drive_state";
    if (topic_name.find("/vehicle/fault_report/") == 0) return "vehicle/fault_report";
    
    // 默认路径: 移除开头的'/'并替换为路径分隔符
    std::string path_name = topic_name;
    if (path_name.front() == '/') path_name = path_name.substr(1);
    std::replace(path_name.begin(), path_name.end(), '/', '_');
    return "receive/" + path_name;
}

/**
 * @brief 从vehicle_config.yaml读取车辆ID
 */
std::string readVIDFromConfig() {
    try {
        YAML::Node config = YAML::LoadFile("car_config/vehicle_config.yaml");
        if (config["vehicle"] && config["vehicle"]["vid"]) {
            std::string vid = config["vehicle"]["vid"].as<std::string>();
            std::cout << "[INFO] 从配置文件读取VID: " << vid << std::endl;
            return vid;
        } else {
            std::cerr << "[ERROR] 配置文件中未找到vehicle.vid字段" << std::endl;
            return "12345678";  // 默认VID
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] 读取配置文件失败: " << e.what() << std::endl;
        std::cerr << "[INFO] 使用默认VID: 12345678" << std::endl;
        return "12345678";  // 默认VID
    }
}

// 异步日志系统
struct LogEntry {
    std::string topic_name;
    std::string content;
    std::chrono::system_clock::time_point timestamp;
};

// AsyncLogger类移除 - 使用通用日志系统
/*
private:
    std::string base_log_dir_;
    std::map<std::string, std::string> topic_log_paths_;
    std::map<std::string, std::ofstream> topic_log_files_;
    
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_thread_;
    std::atomic<bool> should_stop_{false};
    
    void worker_loop() {
        while (!should_stop_ || !log_queue_.empty()) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            queue_cv_.wait(lock, [this] { 
                return !log_queue_.empty() || should_stop_; 
            });
            
            std::vector<LogEntry> batch;
            while (!log_queue_.empty() && batch.size() < 50) {
                batch.push_back(log_queue_.front());
                log_queue_.pop();
            }
            lock.unlock();
            
            for (const auto& entry : batch) {
                write_log_entry(entry);
            }
        }
        
        for (auto& pair : topic_log_files_) {
            if (pair.second.is_open()) {
                pair.second.close();
            }
        }
    }
    
    void write_log_entry(const LogEntry& entry) {
        ensure_topic_log_file(entry.topic_name);
        
        auto& log_file = topic_log_files_[entry.topic_name];
        if (log_file.is_open()) {
            auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                entry.timestamp.time_since_epoch()) % 1000;
            
            log_file << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                     << "." << std::setfill('0') << std::setw(3) << ms.count()
                     << "] " << entry.content << std::endl;
            log_file.flush();
        }
    }
    
    void ensure_topic_log_file(const std::string& topic_name) {
        if (topic_log_files_.find(topic_name) != topic_log_files_.end()) {
            return;
        }
        
        std::string topic_dir;
        std::string log_filename;
        
        if (topic_name == "handshake_request") {
            topic_dir = base_log_dir_ + "/handshake";
            log_filename = "request.log";
        } else if (topic_name == "handshake_response") {
            topic_dir = base_log_dir_ + "/handshake";
            log_filename = "response.log";
        } else if (topic_name == "vehicle_status") {
            topic_dir = base_log_dir_ + "/vehicle";
            log_filename = "vehicle_status.log";
        } else if (topic_name == "control_cmd") {
            topic_dir = base_log_dir_ + "/vehicle";
            log_filename = "control_cmd.log";
        } else if (topic_name == "drive_state") {
            topic_dir = base_log_dir_ + "/vehicle";
            log_filename = "drive_state.log";
        } else if (topic_name == "fault_report") {
            topic_dir = base_log_dir_ + "/vehicle";
            log_filename = "fault_report.log";
        } else {
            topic_dir = base_log_dir_ + "/other";
            log_filename = topic_name + ".log";
        }
        
        std::filesystem::create_directories(topic_dir);
        
        std::string log_path = topic_dir + "/" + log_filename;
        topic_log_paths_[topic_name] = log_path;
        topic_log_files_[topic_name].open(log_path);
        
        if (topic_log_files_[topic_name].is_open()) {
            std::cout << "创建MQTT接收日志文件: " << log_path << std::endl;
        }
    }

public:
    AsyncLogger(const std::string& base_dir) : base_log_dir_(base_dir) {
        worker_thread_ = std::thread(&AsyncLogger::worker_loop, this);
    }
    
    ~AsyncLogger() {
        stop();
    }
    
    void log(const std::string& topic_name, const std::string& content) {
        LogEntry entry;
        entry.topic_name = topic_name;
        entry.content = content;
        entry.timestamp = std::chrono::system_clock::now();
        
        std::lock_guard<std::mutex> lock(queue_mutex_);
        log_queue_.push(entry);
        queue_cv_.notify_one();
    }
    
    void stop() {
        should_stop_ = true;
        queue_cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
    std::map<std::string, std::string> get_log_paths() const {
        return topic_log_paths_;
    }
};
*/

// 全局变量
std::atomic<bool> g_running{true};

// 统计计数器
std::atomic<uint64_t> g_total_received{0};
std::map<std::string, std::atomic<uint64_t>> g_topic_counts;

/**
 * @brief 信号处理函数
 */
void signal_handler(int signal) {
    static bool signal_received = false;
    if (signal_received) return;
    signal_received = true;
    
    std::cout << "\n接收到停止信号，准备退出..." << std::endl;
    g_running = false;
}

// create_log_directory函数已移除，使用通用日志系统

/**
 * @brief MQTT topic接收线程
 */
void mqtt_topic_receiver_thread(const std::string& mqtt_topic, const std::string& topic_name) {
    std::cout << "启动MQTT接收线程: " << mqtt_topic << " -> " << topic_name << std::endl;
    
    // 初始化计数器
    g_topic_counts[topic_name] = 0;
    
    while (g_running) {
        // 使用mosquitto_sub接收消息
        std::string cmd = "timeout 1s mosquitto_sub -h hellorobotaxi.cn -p 11883 -t \"" + mqtt_topic + "\" -C 1 2>/dev/null";
        
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        char buffer[4096];
        std::string message;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            message += buffer;
        }
        
        int result = pclose(pipe);
        
        if (!message.empty() && g_running) {
            // 清理消息末尾的换行符
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
                message.pop_back();
            }
            
            if (!message.empty()) {
                // 更新计数器
                g_topic_counts[topic_name]++;
                g_total_received++;
                
                // 记录日志
                std::ostringstream oss;
                oss << "MQTT接收 [" << mqtt_topic << "] " << message;
                
                LOG_BY_TOPIC(get_topic_log_path(topic_name), "[" + topic_name + "] " + oss.str());
                
                // 控制台输出
                std::cout << "[" << topic_name << "] 接收到消息 (#" 
                          << g_topic_counts[topic_name].load() << ")" << std::endl;
            }
        }
        
        // 短暂休眠，避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "MQTT接收线程结束: " << topic_name 
              << " (总计接收: " << g_topic_counts[topic_name].load() << " 条)" << std::endl;
}

/**
 * @brief 统计显示线程
 */
void statistics_thread() {
    auto start_time = std::chrono::steady_clock::now();
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        if (!g_running) break;
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        
        std::cout << "\n=== MQTT接收统计 (运行时间: " << elapsed << "ms) ===" << std::endl;
        
        uint64_t total = 0;
        for (const auto& pair : g_topic_counts) {
            uint64_t count = pair.second.load();
            total += count;
            std::cout << pair.first << ": " << count 
                      << " (" << (count * 1000.0 / elapsed) << "Hz)" << std::endl;
        }
        
        std::cout << "总计: " << total 
                  << " (" << (total * 1000.0 / elapsed) << "Hz)" << std::endl;
        std::cout << "==============================\n" << std::endl;
    }
}

/**
 * @brief 清理资源
 */
void cleanup() {
    std::cout << "清理资源..." << std::endl;
    
    // 通用日志系统清理
    std::cout << "通用日志系统已自动保存日志" << std::endl;
}

int main() {
    std::cout << "多线程MQTT消息接收器" << std::endl;
    std::cout << "===================" << std::endl;
    std::cout << "从MQTT服务器 (hellorobotaxi.cn:11883) 接收消息" << std::endl;

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化通用日志系统
    initUniversalLogger("mqtt_receiver");
    std::cout << "通用日志系统启动，支持自动清理" << std::endl;

    // MQTT topic映射 (MQTT topic -> 本地topic名)
    // 注意：topic需要加上VID后缀以匹配fastdds_to_mqtt_bridge发布的topic
    std::string vid = readVIDFromConfig();  // 从vehicle_config.yaml动态读取VID
    std::vector<std::pair<std::string, std::string>> topic_mappings = {
        {"/handshake/request/" + vid, "handshake_request"},
        {"/handshake/response/" + vid, "handshake_response"},
        {"/vehicle/vehicle_status/" + vid, "vehicle_status"},
        {"/vehicle/control_cmd/" + vid, "control_cmd"},
        {"/vehicle/drive_state/" + vid, "drive_state"},
        {"/vehicle/fault_report/" + vid, "fault_report"}
    };

    std::cout << "\n启动接收线程..." << std::endl;
    
    // 创建接收线程
    std::vector<std::thread> receiver_threads;
    for (const auto& mapping : topic_mappings) {
        receiver_threads.emplace_back(mqtt_topic_receiver_thread, mapping.first, mapping.second);
    }

    // 启动统计线程
    std::thread stats_thread(statistics_thread);

    std::cout << "开始接收MQTT消息..." << std::endl;
    std::cout << "按 Ctrl+C 停止程序" << std::endl;

    // 主循环
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\n等待接收线程结束..." << std::endl;
    
    // 等待所有线程结束
    for (auto& thread : receiver_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    if (stats_thread.joinable()) {
        stats_thread.join();
    }

    // 最终统计
    std::cout << "\n=== 最终统计 ===" << std::endl;
    uint64_t total = 0;
    for (const auto& pair : g_topic_counts) {
        uint64_t count = pair.second.load();
        total += count;
        std::cout << pair.first << ": " << count << " 条消息" << std::endl;
    }
    std::cout << "总计接收: " << total << " 条消息" << std::endl;

    cleanup();
    cleanupUniversalLogger();
    return 0;
}
