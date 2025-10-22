/**
 * @file fastdds_to_mqtt_bridge.cpp
 * @brief FastDDS到MQTT桥接业务逻辑程序
 * 
 * 功能：
 * - 订阅FastDDS消息
 * - 转换为JSON格式
 * - 发布到MQTT服务器
 * - 异步日志，按topic分类存储
 * 
 * 使用说明:
 * ./fastdds_to_mqtt_bridge
 */

#include "../fastdds_lib/include/fastdds_subscriber.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <mosquitto.h>
#include "logger_conf/universal_logger.h"
 
// 全局订阅者实例
std::unique_ptr<FastDDSSubscriber> g_subscriber = nullptr;
std::atomic<bool> g_running{true};
 
 // 前向声明
 void cleanup();
 
// MQTT相关全局变量
struct mosquitto* g_mosq = nullptr;               // 用于发布到本地MQTT
const char* mqtt_host = "127.0.0.1";  // 连接本地MQTT broker
int mqtt_port = 1883;                 // 本地MQTT端口
const char* client_id = "fastdds_to_mqtt_bridge_8975";
 
 /**
  * @brief MQTT发布函数 - 使用libmosquitto库
  */
 bool publish_mqtt_message(const std::string& topic, const std::string& message) {
     if (!g_mosq) {
         std::cerr << "MQTT客户端未初始化" << std::endl;
         return false;
     }
     
     // 检查连接状态，如果断开则尝试重连
     int socket = mosquitto_socket(g_mosq);
     if (socket == -1) {
         std::cout << "MQTT连接断开，尝试重连..." << std::endl;
         int reconnect_result = mosquitto_reconnect(g_mosq);
         if (reconnect_result != MOSQ_ERR_SUCCESS) {
             std::cerr << "MQTT重连失败: " << mosquitto_strerror(reconnect_result) << std::endl;
             return false;
         }
         std::cout << "MQTT重连成功" << std::endl;
     }
     
     int result = mosquitto_publish(g_mosq, nullptr, topic.c_str(), 
                                  message.length(), message.c_str(), 0, false);
     
     if (result != MOSQ_ERR_SUCCESS) {
         std::cerr << "MQTT发布失败: " << mosquitto_strerror(result) << std::endl;
         return false;
     }
     
     // 立即刷新缓存，强制写入网络
     mosquitto_loop_write(g_mosq, 0);
     
     return true;
 }
 
 /**
  * @brief 获取当前时间戳
  */
 uint64_t getCurrentTimestamp() {
     return std::chrono::duration_cast<std::chrono::milliseconds>(
         std::chrono::system_clock::now().time_since_epoch()).count();
 }
 
/**
 * @brief 初始化MQTT客户端
 */
bool init_mqtt() {
    std::cout << "初始化MQTT客户端..." << std::endl;
    
    // 初始化mosquitto库
    mosquitto_lib_init();
    
    // 创建mosquitto发布客户端实例
    g_mosq = mosquitto_new(client_id, true, nullptr);
    if (!g_mosq) {
        std::cerr << "创建MQTT发布客户端失败" << std::endl;
        return false;
    }
    
    // 连接到MQTT服务器
    int result = mosquitto_connect(g_mosq, mqtt_host, mqtt_port, 60);
    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "连接MQTT发布服务器失败: " << mosquitto_strerror(result) << std::endl;
        return false;
    }
    
    // 启动MQTT网络处理循环
    result = mosquitto_loop_start(g_mosq);
    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "启动MQTT发布循环失败: " << mosquitto_strerror(result) << std::endl;
        return false;
    }
    
    std::cout << "[SUCCESS] MQTT发布客户端初始化成功，连接到 " << mqtt_host << ":" << mqtt_port << std::endl;
    
    return true;
}
 
/**
 * @brief 清理MQTT客户端
 */
void cleanup_mqtt() {
    if (g_mosq) {
        std::cout << "停止MQTT发布网络循环..." << std::endl;
        mosquitto_loop_stop(g_mosq, true);  // 强制停止
        
        std::cout << "断开MQTT发布连接..." << std::endl;
        mosquitto_disconnect(g_mosq);
        
        std::cout << "销毁MQTT发布客户端..." << std::endl;
        mosquitto_destroy(g_mosq);
        g_mosq = nullptr;
    }
    
    std::cout << "清理MQTT库..." << std::endl;
    mosquitto_lib_cleanup();
}
 
 // 异步日志系统
 struct LogEntry {
     std::string topic_name;
     std::string content;
     std::chrono::system_clock::time_point timestamp;
 };
 
 // AsyncLogger类已移除，使用通用日志系统 
 // 全局异步日志器
 
 /**
  * @brief 信号处理函数
  */
 void signal_handler(int signal) {
     static int signal_count = 0;
     signal_count++;
     
     if (signal_count == 1) {
         std::cout << "\n\n接收到停止信号(" << signal << ")，准备优雅退出..." << std::endl;
         g_running = false;
     } else if (signal_count >= 2) {
         std::cout << "\n\n接收到第二次信号，强制退出..." << std::endl;
         cleanup();
         exit(1);
     }
 }
 
 /**
  * @brief FastDDS消息回调处理器
  */
class MessageHandler {
public:
    /**
     * @brief 处理HandshakeResponse消息
     */
     static void on_handshake_response(long noa_active_response, long remote_override_response, bool current_control_source, uint64_t timestamp) {
         uint64_t received_timestamp = getCurrentTimestamp();
         std::string mqtt_message = std::string("{\"noa_active_response\":") + std::to_string(noa_active_response) + 
                                    ",\"remote_override_response\":" + std::to_string(remote_override_response) + 
                                    ",\"current_control_source\":" + (current_control_source ? "true" : "false") + 
                                    ",\"fastdds_timestamp\":" + std::to_string(timestamp / 1000000) +
                                    ",\"received_timestamp\":" + std::to_string(received_timestamp) + "}";
         
        std::string topic = "/handshake/response";
        bool success = publish_mqtt_message(topic, mqtt_message);

        std::cout << topic << " update " << mqtt_message << std::endl;
        
        std::ostringstream oss;
        oss << "FastDDS->MQTT " << (success ? "成功" : "失败") 
            << " [" << topic << "] " << mqtt_message;
   
       LOG_BY_TOPIC("handshake/response", oss.str());
     }
     
    /**
     * @brief 处理VehicleStatus消息 - 接收FastDDS库提供的17个字段
     */
    static void on_vehicle_status(unsigned long vehicle_id, long control_mode, double position_longitude, double position_latitude, float position_altitude, float speed, float yawrate, long gear_position, float acceleration, double heading, float steering_angle, float wheel_angle, long ebrake_status, bool indicator_left, bool indicator_right, uint64_t timestamp, int power_mode) {
         uint64_t received_timestamp = getCurrentTimestamp();
         std::string mqtt_message = "{\"vehicle_id\":" + std::to_string(vehicle_id) +
                                    ",\"control_mode\":" + std::to_string(control_mode) +
                                    ",\"position_longitude\":" + std::to_string(position_longitude) +
                                    ",\"position_latitude\":" + std::to_string(position_latitude) +
                                    ",\"position_altitude\":" + std::to_string(position_altitude) +
                                    ",\"speed\":" + std::to_string(speed) +
                                    ",\"yawrate\":" + std::to_string(yawrate) +
                                    ",\"gear_position\":" + std::to_string(gear_position) +
                                    ",\"acceleration\":" + std::to_string(acceleration) +
                                    ",\"heading\":" + std::to_string(heading) +
                                    ",\"steering_angle\":" + std::to_string(steering_angle) +
                                    ",\"wheel_angle\":" + std::to_string(wheel_angle) +
                                   ",\"ebrake_status\":" + std::to_string(ebrake_status) +
                                   ",\"indicator_left\":" + (indicator_left ? "true" : "false") +
                                   ",\"indicator_right\":" + (indicator_right ? "true" : "false") +
                                   ",\"fastdds_timestamp\":" + std::to_string(timestamp / 1000000) +
                                    ",\"received_timestamp\":" + std::to_string(received_timestamp) +
                                    ",\"power_mode\":" + std::to_string(power_mode) + "}";
         
        std::string topic = "/vehicle/vehicle_status";
        bool success = publish_mqtt_message(topic, mqtt_message);
        
        std::cout << topic << " update " << mqtt_message << std::endl;
        
        std::ostringstream oss;
        oss << "FastDDS->MQTT " << (success ? "成功" : "失败") 
            << " [" << topic << "] " << mqtt_message;
        
        LOG_BY_TOPIC("vehicle/vehicle_status", oss.str());
    }

    /**
     * @brief 处理DriveState消息
     */
     static void on_drive_state(long driving_state, uint64_t timestamp) {
         uint64_t received_timestamp = getCurrentTimestamp();
         std::string mqtt_message = "{\"driving_state\":" + std::to_string(driving_state) + 
                                    ",\"timestamp\":" + std::to_string(timestamp / 1000000) +
                                    ",\"received_timestamp\":" + std::to_string(received_timestamp) + "}";
         
        std::string topic = "/vehicle/drive_state";
        bool success = publish_mqtt_message(topic, mqtt_message);
        
        std::cout << topic << " update " << mqtt_message << std::endl;
        
        std::ostringstream oss;
        oss << "FastDDS->MQTT " << (success ? "成功" : "失败") 
            << " [" << topic << "] " << mqtt_message;
        
        LOG_BY_TOPIC("vehicle/drive_state", oss.str());
     }
 
     /**
      * @brief 处理FaultReport消息
      */
     static void on_fault_report(const std::vector<int32_t>& fault_types, uint64_t timestamp) {
         uint64_t received_timestamp = getCurrentTimestamp();
         
         // 构建故障类型数组的JSON
         std::string fault_array = "[";
         for (size_t i = 0; i < fault_types.size(); ++i) {
             if (i > 0) fault_array += ",";
             fault_array += std::to_string(fault_types[i]);
         }
         fault_array += "]";
         
         std::string mqtt_message = "{\"fault_type\":" + fault_array + 
                                    ",\"fault_count\":" + std::to_string(fault_types.size()) +
                                    ",\"timestamp\":" + std::to_string(timestamp / 1000000) +
                                    ",\"received_timestamp\":" + std::to_string(received_timestamp) + "}";
         
        std::string topic = "/vehicle/fault_report";
        bool success = publish_mqtt_message(topic, mqtt_message);
        
        std::cout << topic << " update " << mqtt_message << std::endl;
        
        std::ostringstream oss;
        oss << "FastDDS->MQTT " << (success ? "成功" : "失败") 
            << " [" << topic << "] " << mqtt_message;
        
        LOG_BY_TOPIC("vehicle/fault_report", oss.str());
     }
 };
 
 // create_log_directory函数已移除，使用通用日志系统
 
/**
 * @brief 初始化订阅者
 */
bool init_subscriber() {
    std::cout << "初始化FastDDS订阅者 [域:0]" << std::endl;
    
    g_subscriber = std::make_unique<FastDDSSubscriber>(0);
    
    if (!g_subscriber) {
        std::cerr << "创建订阅者失败" << std::endl;
        return false;
    }
    
    // 设置消息回调处理器 - 只处理上行消息（车端->云端）
    g_subscriber->setHandshakeResponseCallback(MessageHandler::on_handshake_response);
    g_subscriber->setVehicleStatusCallback(MessageHandler::on_vehicle_status);
    g_subscriber->setDriveStateCallback(MessageHandler::on_drive_state);
    g_subscriber->setFaultReportCallback(MessageHandler::on_fault_report);
    
    // 初始化订阅者并订阅所有主题
    if (!g_subscriber->init()) {
        std::cerr << "FastDDS订阅者初始化失败" << std::endl;
        return false;
    }
    
    std::cout << "FastDDS订阅者初始化成功" << std::endl;
    return true;
}
 
 /**
  * @brief 清理资源
  */
 void cleanup() {
     static bool cleanup_called = false;
     if (cleanup_called) return;
     cleanup_called = true;
     
     std::cout << "\n程序正在退出..." << std::endl;
     
     // 1. 首先清理MQTT客户端（停止网络线程）
     cleanup_mqtt();
     
     // 2. 清理FastDDS订阅者
     if (g_subscriber) {
         std::cout << "清理FastDDS订阅者..." << std::endl;
         g_subscriber.reset();
     }
     
     // 3. 通用日志系统清理
     std::cout << "通用日志系统已自动保存日志" << std::endl;
     
     std::cout << "[SUCCESS] 清理完成" << std::endl;
 }
 
int main(int argc, char* argv[]) {
    std::cout << "FastDDS到MQTT桥接器 (上行链路：车端->云端)" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "接收FastDDS消息并转发到本地MQTT (127.0.0.1:1883)" << std::endl;
    std::cout << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化通用日志系统
    initUniversalLogger("fastdds_to_mqtt_bridge");
    std::cout << "通用日志系统启动，支持自动清理" << std::endl;
 
     // 初始化MQTT客户端
     if (!init_mqtt()) {
         cleanup();
         return 1;
     }
 
     if (!init_subscriber()) {
         cleanup();
         return 1;
     }
 
     std::cout << "\n开始监听FastDDS消息..." << std::endl;
     std::cout << "按 Ctrl+C 停止程序" << std::endl;
 
     // 主循环
     while (g_running) {
         std::this_thread::sleep_for(std::chrono::milliseconds(100));
     }
 
     cleanup();
     cleanupUniversalLogger();
     return 0;
 }
 