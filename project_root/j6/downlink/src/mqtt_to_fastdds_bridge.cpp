/**
 * @file mqtt_to_fastdds_bridge.cpp
 * @brief J6下行系统 - MQTT到FastDDS桥接器
 * 
 * 功能：
 * - 订阅MQTT消息（来自cloud_mqtt_sender）
 * - 解析JSON数据
 * - 转换为FastDDS消息并发布
 * - 异步日志，按topic分类存储
 * 
 * 使用说明：
 * ./mqtt_to_fastdds_bridge
 */

#include <fastrtps/participant/Participant.h>
#include <fastrtps/attributes/ParticipantAttributes.h>
#include <fastrtps/publisher/Publisher.h>
#include <fastrtps/attributes/PublisherAttributes.h>
#include <fastrtps/Domain.h>

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
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <map>
#include <mosquitto.h>
#include <json/json.h>

// 包含生成的IDL类型支持头文件
#include "Handshake.h"
#include "HandshakePubSubTypes.h"
#include "RemoteControl.h"
#include "RemoteControlPubSubTypes.h"
#include "VehicleStatus.h"
#include "VehicleStatusPubSubTypes.h"
#include "AsyncLogger.h"

using namespace eprosima::fastrtps;
using namespace eprosima::fastrtps::rtps;

// 全局控制变量
std::atomic<bool> g_running{true};
std::unique_ptr<AsyncLogger> g_logger = nullptr;

// MQTT相关全局变量
struct mosquitto* g_mosq = nullptr;
const char* mqtt_host = "hellorobotaxi.cn";
int mqtt_port = 11883;
const char* client_id = "j6_mqtt_to_fastdds_bridge";

// FastDDS相关变量
Participant* mp_participant = nullptr;
Publisher* mp_handshake_req_publisher = nullptr;
Publisher* mp_handshake_resp_publisher = nullptr;
Publisher* mp_control_publisher = nullptr;
Publisher* mp_status_publisher = nullptr;

// 类型支持
remote_msgs::HandshakeRequestPubSubType m_handshake_req_type;
remote_msgs::HandshakeResponsePubSubType m_handshake_resp_type;
remote_msgs::ControlCmdPubSubType m_control_type;
remote_msgs::VehicleStatusPubSubType m_status_type;

// 消息序列号
std::atomic<uint64_t> g_message_sequence{0};

uint64_t getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string get_topic_log_name(const std::string& topic) {
    if (topic == "/vehicle/control_cmd") return "vehicle_control_cmd";
    if (topic == "/handshake/request") return "handshake_request";
    if (topic == "/handshake/response") return "handshake_response";
    if (topic == "/vehicle/vehicle_status") return "vehicle_vehicle_status";
    return "unknown_" + topic;
}

bool init_fastdds() {
    std::cout << "初始化FastDDS发布者..." << std::endl;
    
    // 创建参与者
    ParticipantAttributes PParam;
    PParam.rtps.builtin.discovery_config.discoveryProtocol = DiscoveryProtocol_t::SIMPLE;
    PParam.rtps.builtin.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol = true;
    PParam.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationReaderANDSubscriptionWriter = true;
    PParam.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationWriterANDSubscriptionReader = true;
    PParam.rtps.setName("J6MQTTToFastDDSBridge");
    
    mp_participant = Domain::createParticipant(PParam);
    if (mp_participant == nullptr) {
        std::cout << "[ERROR] 创建参与者失败!" << std::endl;
        return false;
    }
    
    // 注册类型
    Domain::registerType(mp_participant, &m_handshake_req_type);
    Domain::registerType(mp_participant, &m_handshake_resp_type);
    Domain::registerType(mp_participant, &m_control_type);
    Domain::registerType(mp_participant, &m_status_type);
    
    // 创建发布者
    PublisherAttributes Wparam;
    
    // HandshakeRequest发布者
    Wparam.topic.topicKind = NO_KEY;
    Wparam.topic.topicDataType = "remote_msgs::HandshakeRequest";
    Wparam.topic.topicName = "handshake/request";
    Wparam.topic.historyQos.kind = KEEP_LAST_HISTORY_QOS;
    Wparam.topic.historyQos.depth = 10;
    Wparam.qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;
    mp_handshake_req_publisher = Domain::createPublisher(mp_participant, Wparam);
    if (mp_handshake_req_publisher == nullptr) return false;
    
    // HandshakeResponse发布者
    Wparam.topic.topicDataType = "remote_msgs::HandshakeResponse";
    Wparam.topic.topicName = "handshake/response";
    mp_handshake_resp_publisher = Domain::createPublisher(mp_participant, Wparam);
    if (mp_handshake_resp_publisher == nullptr) return false;
    
    // ControlCmd发布者
    Wparam.topic.topicDataType = "remote_msgs::ControlCmd";
    Wparam.topic.topicName = "vehicle/control_cmd";
    mp_control_publisher = Domain::createPublisher(mp_participant, Wparam);
    if (mp_control_publisher == nullptr) return false;
    
    // VehicleStatus发布者
    Wparam.topic.topicDataType = "remote_msgs::VehicleStatus";
    Wparam.topic.topicName = "vehicle/vehicle_status";
    mp_status_publisher = Domain::createPublisher(mp_participant, Wparam);
    if (mp_status_publisher == nullptr) return false;
    
    std::cout << "[SUCCESS] FastDDS发布者初始化完成!" << std::endl;
    return true;
}

bool parse_and_publish_handshake_request(const std::string& json_str) {
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(json_str, root)) return false;
        
        remote_msgs::HandshakeRequest msg;
        msg.noa_active_request(root.get("noa_active", 0).asInt());
        msg.remote_override_status(root.get("override_status", 0).asInt());
        msg.remote_override_ready(root.get("override_ready", 0).asInt());
        msg.timestamp(getCurrentTimestamp());
        
        return mp_handshake_req_publisher->write(&msg);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] 解析HandshakeRequest失败: " << e.what() << std::endl;
        return false;
    }
}

bool parse_and_publish_handshake_response(const std::string& json_str) {
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(json_str, root)) return false;
        
        remote_msgs::HandshakeResponse msg;
        msg.noa_active_response(root.get("noa_active", 0).asInt());
        msg.remote_override_response(root.get("override_response", 0).asInt());
        msg.current_control_source(root.get("control_source", false).asBool());
        msg.timestamp(getCurrentTimestamp());
        
        return mp_handshake_resp_publisher->write(&msg);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] 解析HandshakeResponse失败: " << e.what() << std::endl;
        return false;
    }
}

bool parse_and_publish_control_cmd(const std::string& json_str) {
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(json_str, root)) return false;
        
        remote_msgs::ControlCmd msg;
        msg.steering_angle_enable(root.get("steering_angle_enable", 0.0).asDouble() != 0.0);
        msg.steering_angle(root.get("steering_angle", 0.0).asFloat());
        msg.target_acceleration_enable(root.get("target_acceleration_enable", 0.0).asDouble() != 0.0);
        msg.target_acceleration(root.get("target_acceleration", 0.0).asFloat());
        msg.indicator_left_enable(root.get("indicator_left_enable", 0.0).asDouble() != 0.0);
        msg.indicator_left(root.get("indicator_left", 0.0).asDouble() != 0.0);
        msg.indicator_right_enable(root.get("indicator_right_enable", 0.0).asDouble() != 0.0);
        msg.indicator_right(root.get("indicator_right", 0.0).asDouble() != 0.0);
        msg.gear_position_enable(root.get("gear_position_enable", 0.0).asDouble() != 0.0);
        msg.gear_position(root.get("gear_position", 0).asInt64());
        msg.ebrake_status_enable(root.get("ebrake_status_enable", 0.0).asDouble() != 0.0);
        msg.ebrake_status(root.get("ebrake_status", 0).asInt64());
        msg.timestamp(getCurrentTimestamp());
        
        return mp_control_publisher->write(&msg);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] 解析ControlCmd失败: " << e.what() << std::endl;
        return false;
    }
}

bool parse_and_publish_vehicle_status(const std::string& json_str) {
    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(json_str, root)) return false;
        
        remote_msgs::VehicleStatus msg;
        msg.vehicle_id(root.get("vehicle_id", 0).asUInt());
        
        // 转换control_mode为枚举类型
        int control_mode_val = root.get("control_mode", 0).asInt();
        msg.control_mode(static_cast<remote_msgs::ControlMode>(control_mode_val));
        
        msg.position_longitude(root.get("position_longitude", 0.0).asFloat());
        msg.position_latitude(root.get("position_latitude", 0.0).asFloat());
        msg.position_altitude(root.get("position_altitude", 0.0).asFloat());
        msg.speed(root.get("speed", 0.0).asFloat());
        msg.yawrate(root.get("yawrate", 0.0).asFloat());
        msg.gear_position(root.get("gear_position", 0.0).asFloat());
        msg.acceleration(root.get("acceleration", 0.0).asFloat());
        msg.heading(root.get("heading", 0.0).asFloat());
        msg.steering_angle(root.get("steering_angle", 0.0).asFloat());
        msg.wheel_angle(root.get("wheel_angle", 0.0).asFloat());
        msg.ebrake_status(root.get("ebrake_status", 0).asInt64());
        msg.indicator_left(root.get("indicator_left", 0).asInt64());
        msg.indicator_right(root.get("indicator_right", 0).asInt64());
        msg.timestamp(getCurrentTimestamp());
        
        // 转换power_mode为枚举类型
        int power_mode_val = root.get("power_mode", 0).asInt();
        msg.power_mode(static_cast<remote_msgs::PowerModeType>(power_mode_val));
        
        return mp_status_publisher->write(&msg);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] 解析VehicleStatus失败: " << e.what() << std::endl;
        return false;
    }
}

void on_mqtt_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message) {
    if (message->payloadlen == 0) return;
    
    std::string topic(message->topic);
    std::string payload((char*)message->payload, message->payloadlen);
    
    g_message_sequence++;
    bool conversion_success = false;
    std::string log_content;
    
    // 根据topic进行转换
    if (topic == "/handshake/request") {
        conversion_success = parse_and_publish_handshake_request(payload);
    } else if (topic == "/handshake/response") {
        conversion_success = parse_and_publish_handshake_response(payload);
    } else if (topic == "/vehicle/control_cmd") {
        conversion_success = parse_and_publish_control_cmd(payload);
    } else if (topic == "/vehicle/vehicle_status") {
        conversion_success = parse_and_publish_vehicle_status(payload);
    } else {
        std::cout << "[WARN] 未知topic: " << topic << std::endl;
        return;
    }
    
    // 记录日志
    log_content = "MQTT→FastDDS转换" + std::string(conversion_success ? "成功" : "失败") + 
                  " [" + topic + "] " + payload;
    
    if (g_logger) {
        std::string topic_log_name = get_topic_log_name(topic);
        g_logger->logMessage(topic_log_name, log_content, getCurrentTimestamp(), g_message_sequence);
    }
    
    std::cout << "[" << (conversion_success ? "SUCCESS" : "ERROR") << "] " << log_content << std::endl;
}

bool init_mqtt() {
    mosquitto_lib_init();
    g_mosq = mosquitto_new(client_id, true, nullptr);
    if (!g_mosq) return false;
    
    mosquitto_message_callback_set(g_mosq, on_mqtt_message);
    
    if (mosquitto_connect(g_mosq, mqtt_host, mqtt_port, 60) != MOSQ_ERR_SUCCESS) return false;
    
    // 订阅所有相关topic
    mosquitto_subscribe(g_mosq, nullptr, "/handshake/request", 0);
    mosquitto_subscribe(g_mosq, nullptr, "/handshake/response", 0);
    mosquitto_subscribe(g_mosq, nullptr, "/vehicle/control_cmd", 0);
    mosquitto_subscribe(g_mosq, nullptr, "/vehicle/vehicle_status", 0);
    
    if (mosquitto_loop_start(g_mosq) != MOSQ_ERR_SUCCESS) return false;
    
    std::cout << "[SUCCESS] MQTT客户端连接并订阅成功" << std::endl;
    return true;
}

void cleanup_fastdds() {
    if (mp_handshake_req_publisher) {
        Domain::removePublisher(mp_handshake_req_publisher);
    }
    if (mp_handshake_resp_publisher) {
        Domain::removePublisher(mp_handshake_resp_publisher);
    }
    if (mp_control_publisher) {
        Domain::removePublisher(mp_control_publisher);
    }
    if (mp_status_publisher) {
        Domain::removePublisher(mp_status_publisher);
    }
    if (mp_participant) {
        Domain::removeParticipant(mp_participant);
    }
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

void signal_handler(int signal) {
    static int signal_count = 0;
    signal_count++;
    
    if (signal_count == 1) {
        std::cout << "\n接收到停止信号，准备退出..." << std::endl;
        g_running = false;
    } else {
        std::cout << "\n强制退出..." << std::endl;
        cleanup_mqtt();
        cleanup_fastdds();
        if (g_logger) {
            g_logger->shutdown();
            g_logger.reset();
        }
        exit(1);
    }
}

void cleanup() {
    cleanup_mqtt();
    cleanup_fastdds();
    if (g_logger) {
        g_logger->shutdown();
        g_logger.reset();
    }
    std::cout << "清理完成" << std::endl;
}

int main() {
    std::cout << "=== J6下行系统 - MQTT到FastDDS桥接器 ===" << std::endl;
    std::cout << "订阅MQTT消息并转换为FastDDS消息" << std::endl;
    std::cout << "MQTT服务器: " << mqtt_host << ":" << mqtt_port << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化异步日志系统
    g_logger = std::make_unique<AsyncLogger>();
    if (!g_logger->init("/home/wjj/work/project_root/j6/downlink/logs")) {
        std::cerr << "[ERROR] 异步日志系统初始化失败!" << std::endl;
        return 1;
    }

    // 初始化FastDDS
    if (!init_fastdds()) {
        std::cerr << "[ERROR] FastDDS初始化失败!" << std::endl;
        cleanup();
        return 1;
    }

    // 初始化MQTT
    if (!init_mqtt()) {
        std::cerr << "[ERROR] MQTT初始化失败!" << std::endl;
        cleanup();
        return 1;
    }

    std::cout << "\n开始监听MQTT消息并转换为FastDDS..." << std::endl;
    std::cout << "订阅的MQTT主题:" << std::endl;
    std::cout << "  - /handshake/request" << std::endl;
    std::cout << "  - /handshake/response" << std::endl;
    std::cout << "  - /vehicle/control_cmd" << std::endl;
    std::cout << "  - /vehicle/vehicle_status" << std::endl;
    std::cout << "\n按 Ctrl+C 停止程序" << std::endl;

    // 主循环
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    cleanup();
    return 0;
}
