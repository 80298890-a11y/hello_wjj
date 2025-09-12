/**
 * @file mqtt_to_fastdds_bridge.cpp
 * @brief MQTT到FastDDS桥接业务逻辑程序
 * 
 * 功能：
 * - 订阅MQTT消息（来自mqtt_sender）
 * - 解析JSON数据
 * - 转换为FastDDS消息并发布
 * - 异步日志，按topic分类存储
 * 
 * 使用说明：
 * ./mqtt_to_fastdds_bridge
 */

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
#include <algorithm>
#include "logger_conf/universal_logger.h"

/**
 * @brief 获取topic对应的日志路径（用于按topic分类日志）
 */
std::string get_topic_log_path(const std::string& topic_name) {
    // 只处理核心通信主题的日志路径
    if (topic_name == "/handshake/request") return "handshake/request";
    if (topic_name == "/handshake/response") return "handshake/response";
    if (topic_name == "/vehicle/control_cmd") return "vehicle/control_cmd";
    
    // 处理带VID的topic
    if (topic_name.find("/handshake/request/") == 0) return "handshake/request";
    if (topic_name.find("/handshake/response/") == 0) return "handshake/response";
    if (topic_name.find("/vehicle/control_cmd/") == 0) return "vehicle/control_cmd";
    
    // 其他topic - 通常不应该出现，因为我们只订阅上述三个
    std::string path_name = topic_name;
    if (!path_name.empty() && path_name.front() == '/') path_name = path_name.substr(1);
    std::replace(path_name.begin(), path_name.end(), '/', '_');
    return "bridge/" + path_name;
}

// 全局控制变量
std::unique_ptr<MQTTToFastDDSConverter> g_converter = nullptr;
std::atomic<bool> g_running{true};

// 前向声明
void cleanup();

// 旧的异步日志系统已移除，现在使用通用日志系统


/**
 * @brief MQTT消息回调处理器
 */
void on_mqtt_message_received(const std::string& topic, const std::string& message, bool conversion_success) {
    // 统计转换结果
    static std::map<std::string, int> success_count;
    static std::map<std::string, int> total_count;
    
    total_count[topic]++;
    
    // 根据转换结果更新成功计数
    if (conversion_success) {
        success_count[topic]++;
    }
    
    // 记录接收到MQTT消息的日志
    std::string topic_path = get_topic_log_path(topic);
    std::ostringstream log_content;
    log_content << "MQTT接收成功 [" << topic << "] → FastDDS转换" 
                << (conversion_success ? "成功" : "失败") << ": " << message;
    
    LOG_BY_TOPIC(topic_path, log_content.str());
    
    // 显示统计信息（每10条消息显示一次）
    if (total_count[topic] % 10 == 0) {
        std::cout << "\n=== 转换统计 [" << topic << "] ===" << std::endl;
        std::cout << "总计: " << total_count[topic] << " 条" << std::endl;
        std::cout << "成功: " << success_count[topic] << " 条" << std::endl;
        std::cout << "失败: " << (total_count[topic] - success_count[topic]) << " 条" << std::endl;
        std::cout << "==========================================\n" << std::endl;
    }
}

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
 * @brief 清理资源
 */
void cleanup() {
    static bool cleanup_called = false;
    if (cleanup_called) return;
    cleanup_called = true;
    
    std::cout << "\n程序正在退出..." << std::endl;
    
    // 1. 清理转换器（包含MQTT客户端和FastDDS发布者）
    if (g_converter) {
        std::cout << "清理MQTT到FastDDS转换器..." << std::endl;
        g_converter.reset();
    }
    
    // 2. 通用日志系统清理
    std::cout << "通用日志系统已自动保存日志" << std::endl;
    
    std::cout << "清理完成" << std::endl;
}

/**
 * @brief 显示转换器状态
 */
void display_converter_status() {
    if (g_converter) {
        std::cout << "MQTT状态: " << g_converter->getMQTTStatusString() << std::endl;
        std::cout << "FastDDS状态: " << (g_converter->isMQTTConnected() ? "已初始化" : "未初始化") << std::endl;
    }
}

int main() {
    std::cout << "MQTT到FastDDS桥接器" << std::endl;
    std::cout << "===================" << std::endl;
    std::cout << "订阅MQTT消息并转换为FastDDS消息 (hellorobotaxi.cn:11883 → 本地FastDDS域)" << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化通用日志系统
    initUniversalLogger("mqtt_to_fastdds_bridge");
    std::cout << "通用日志系统启动，支持自动清理" << std::endl;

    // 创建MQTT到FastDDS转换器
    g_converter = std::make_unique<MQTTToFastDDSConverter>(0, "hellorobotaxi.cn", 11883, "mqtt_to_fastdds_bridge");
    
    // 设置MQTT消息回调
    g_converter->setMQTTMessageCallback(on_mqtt_message_received);

    // 初始化转换器
    if (!g_converter->init()) {
        std::cerr << "[ERROR] MQTT到FastDDS转换器初始化失败" << std::endl;
        cleanup();
        return 1;
    }

    std::cout << "\n开始监听MQTT消息并转发核心通信主题..." << std::endl;
    std::cout << "仅订阅以下MQTT主题:" << std::endl;
    std::cout << "  - /handshake/request" << std::endl;
    std::cout << "  - /handshake/response" << std::endl;
    std::cout << "  - /vehicle/control_cmd" << std::endl;
    std::cout << "❌ 其他消息将被忽略，不进行转发" << std::endl;
    
    // 额外等待确保所有订阅都已建立
    std::cout << "\n等待MQTT订阅完全建立..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "[READY] 桥接器准备完毕，可以接收MQTT消息" << std::endl;
    std::cout << "\n按 Ctrl+C 停止程序" << std::endl;

    // 显示初始状态
    display_converter_status();

    // 主循环
    auto last_status_time = std::chrono::steady_clock::now();
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 每30秒显示一次状态
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_status_time).count() >= 30) {
            std::cout << "\n=== 桥接器状态 ===" << std::endl;
            display_converter_status();
            std::cout << "==================" << std::endl;
            last_status_time = now;
        }
    }

    cleanup();
    cleanupUniversalLogger();
    return 0;
}