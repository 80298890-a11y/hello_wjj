/**
 * @file publisher.cpp  
 * @brief 简化的FastDDS发布者 - 模拟车端发送消息
 */

#include "fastdds_publisher.hpp"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <signal.h>
#include <atomic>
#include "logger_conf/universal_logger.h"
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>

std::unique_ptr<FastDDSPublisher> g_publisher = nullptr;
std::atomic<bool> g_running{true};

/**
 * @brief 按topic分类的日志记录器  
 */
class TopicLogger {
private:
    std::string session_dir_;
    std::map<std::string, std::ofstream> topic_files_;
    
public:
    TopicLogger() {
        // 创建session日志目录
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
        session_dir_ = "logs/fastdds/pub/" + oss.str();
        
        std::filesystem::create_directories(session_dir_);
        std::cout << "日志目录: " << session_dir_ << std::endl;
    }
    
    ~TopicLogger() {
        for (auto& pair : topic_files_) {
            if (pair.second.is_open()) {
                pair.second.close();
            }
        }
    }
    
    void log(const std::string& topic_path, const std::string& message) {
        // 确保topic日志文件存在
        if (topic_files_.find(topic_path) == topic_files_.end()) {
            std::string log_file_path = session_dir_ + "/" + topic_path + ".log";
            
            // 创建子目录
            std::filesystem::path log_path(log_file_path);
            std::filesystem::create_directories(log_path.parent_path());
            
            topic_files_[topic_path].open(log_file_path);
        }
        
        auto& log_file = topic_files_[topic_path];
        if (!log_file.is_open()) return;
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        log_file << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                 << "." << std::setfill('0') << std::setw(3) << ms.count()
                 << "] " << message << std::endl;
        log_file.flush();
    }
    
    std::string get_topic_path(const std::string& topic_name) {
        if (topic_name == "handshake_request") {
            return "handshake/request";
        } else if (topic_name == "handshake_response") {
            return "handshake/response";
        } else if (topic_name == "vehicle_status") {
            return "vehicle/vehicle_status";
        } else if (topic_name == "remote_control") {
            return "vehicle/control_cmd";
        } else if (topic_name == "drive_state") {
            return "vehicle/drive_state";
        } else if (topic_name == "fault_report") {
            return "vehicle/fault_report";
        }
        return "unknown/" + topic_name;
    }
};

// TopicLogger已移除，使用通用日志系统

void signal_handler(int signal) {
    std::cout << "\n收到停止信号，退出程序..." << std::endl;
    g_running = false;
}

/**
 * @brief 发送测试消息
 */
void send_test_messages() {
    static int count = 0;
    count++;
    
    std::cout << "\n=== 发送第 " << count << " 轮车端消息（4个IDL） ===" << std::endl;
    
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // 1. 测试原有功能 - 发送握手请求 (临时测试)
    if (g_publisher->publishHandshakeRequest("/handshake/request", 1, 0, 1, timestamp)) {
        std::string msg = "HandshakeRequest发送成功: timestamp=" + std::to_string(timestamp);
        std::cout << "✓ " << msg << std::endl;
        LOG_INFO("[handshake_request] " + msg);
    }
    
    // 1. 发送握手响应 (车端发送)
    long noa_response = (count % 2 == 0) ? 1 : 0;
    long remote_override_response = (count % 3 == 0) ? 1 : -1;
    bool current_control_source = (count % 2 == 1);
    if (g_publisher->publishHandshakeResponse("/handshake/response", noa_response, remote_override_response, current_control_source, timestamp)) {
        std::string msg = "HandshakeResponse发送成功: noa_response=" + std::to_string(noa_response) + 
                         ", remote_override_response=" + std::to_string(remote_override_response) + 
                         ", current_control_source=" + std::to_string(current_control_source) + 
                         ", timestamp=" + std::to_string(timestamp);
        std::cout << "✓ " << msg << std::endl;
        LOG_INFO("[handshake_response] " + msg);
    }
    
    // 2. 发送车辆状态 (车端发送)
    float longitude = 116.3974f + (count % 10) * 0.001f;
    float latitude = 39.9093f + (count % 10) * 0.001f;
    float heading = (count % 360) * 1.0f;
    float speed = 10.0f + (count % 20) * 2.0f;
    float steering_angle = -15.0f + (count % 30);
    float gear = 3.0f + (count % 3);
    if (g_publisher->publishVehicleStatus("/vehicle/vehicle_status", longitude, latitude, heading, speed, steering_angle, gear, timestamp)) {
        std::string msg = "VehicleStatus发送成功: pos=(" + std::to_string(longitude) + "," + std::to_string(latitude) + 
                         "), heading=" + std::to_string(heading) + ", speed=" + std::to_string(speed) + 
                         ", steering=" + std::to_string(steering_angle) + ", gear=" + std::to_string(gear) + 
                         ", timestamp=" + std::to_string(timestamp);
        std::cout << "✓ " << msg << std::endl;
        LOG_INFO("[vehicle_status] " + msg);
    }
    
    // 3. 发送驾驶状态 (车端发送)
    long driving_state = count % 5; // 0-4循环
    if (g_publisher->publishDriveState("/vehicle/drive_state", driving_state)) {
        std::string msg = "DriveState发送成功: driving_state=" + std::to_string(driving_state);
        std::cout << "✓ " << msg << std::endl;
        LOG_INFO("[drive_state] " + msg);
    }
    
    // 4. 发送故障报告 (车端发送)
    std::vector<long> fault_types;
    if (count % 5 == 0) {
        fault_types = {101, 202}; // 模拟一些故障码
    } else if (count % 7 == 0) {
        fault_types = {303};
    }
    // 其他情况发送空故障列表
    if (g_publisher->publishFaultReport("/vehicle/fault_report", fault_types)) {
        std::string msg = "FaultReport发送成功: fault_count=" + std::to_string(fault_types.size());
        if (!fault_types.empty()) {
            msg += ", faults=[";
            for (size_t i = 0; i < fault_types.size(); ++i) {
                if (i > 0) msg += ",";
                msg += std::to_string(fault_types[i]);
            }
            msg += "]";
        }
        std::cout << "✓ " << msg << std::endl;
        LOG_INFO("[fault_report] " + msg);
    }
}

int main(int argc, char* argv[])
{
    std::cout << "=== 简化版FastDDS发布者 - 车端消息发送测试 ===" << std::endl;
    
    // 初始化通用日志系统
    initUniversalLogger("publisher");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化发布者
    std::cout << "初始化FastDDS发布者..." << std::endl;
    g_publisher = std::make_unique<FastDDSPublisher>();
    if (!g_publisher->init()) {
        std::cerr << "发布者初始化失败" << std::endl;
        return 1;
    }
    std::cout << "✓ 发布者初始化成功\n" << std::endl;
    
    if (argc > 1 && std::string(argv[1]) == "once") {
        // 单次发送模式
        std::cout << "=== 单次发送模式 ===" << std::endl;
        send_test_messages();
        std::cout << "\n单次发送完成" << std::endl;
    } else {
        // 循环发送模式  
        std::cout << "=== 循环发送模式 (每2秒发送一轮) ===" << std::endl;
        std::cout << "按 Ctrl+C 停止\n" << std::endl;
        
        while (g_running) {
            send_test_messages();
            
            // 等待2秒
            for (int i = 0; i < 20 && g_running; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
    
    std::cout << "\n清理资源..." << std::endl;
    g_publisher.reset();
    std::cout << "程序退出" << std::endl;
    
    cleanupUniversalLogger();
    return 0;
}
