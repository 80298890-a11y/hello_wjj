/**
 * @file vehicle_receiver.cpp
 * @brief J6下行系统 - 车机接收器
 * 
 * 功能：
 * - 订阅所有FastDDS控制消息
 * - 记录接收到的消息
 * - 统计接收频率
 * - 异步日志，按topic分类存储
 * 
 * 使用说明：
 * ./vehicle_receiver [config_file]
 */

#include <fastrtps/participant/Participant.h>
#include <fastrtps/attributes/ParticipantAttributes.h>
#include <fastrtps/subscriber/Subscriber.h>
#include <fastrtps/attributes/SubscriberAttributes.h>
#include <fastrtps/subscriber/SubscriberListener.h>
#include <fastrtps/subscriber/SampleInfo.h>
#include <fastrtps/Domain.h>

#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <atomic>
#include <filesystem>

// 包含生成的IDL类型支持头文件
#include "Handshake.h"
#include "HandshakePubSubTypes.h"
#include "RemoteControl.h"
#include "RemoteControlPubSubTypes.h"
#include "VehicleStatus.h"
#include "VehicleStatusPubSubTypes.h"
#include "AsyncLogger.h"
#include "ConfigManager.h"

using namespace eprosima::fastrtps;
using namespace eprosima::fastrtps::rtps;

// 全局控制变量
std::atomic<bool> g_running{true};
std::unique_ptr<AsyncLogger> g_logger = nullptr;
std::unique_ptr<ConfigManager> g_config = nullptr;

// 统计计数器
std::atomic<uint64_t> g_handshake_request_count{0};
std::atomic<uint64_t> g_handshake_response_count{0};
std::atomic<uint64_t> g_vehicle_status_count{0};
std::atomic<uint64_t> g_control_cmd_count{0};

// 消息序列号计数器
std::atomic<uint64_t> g_message_sequence{0};

uint64_t getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// 通用的订阅者监听器模板
template<typename MessageType>
class MessageSubscriberListener : public SubscriberListener {
private:
    std::string m_topic_name;
    std::atomic<uint64_t> m_message_count{0};
    
public:
    MessageSubscriberListener(const std::string& topic_name) : m_topic_name(topic_name) {}
    
    void onSubscriptionMatched(Subscriber* /*sub*/, MatchingInfo& info) override {
        if (info.status == MATCHED_MATCHING) {
            std::cout << "[INFO] 订阅者匹配成功: " << m_topic_name << std::endl;
        } else {
            std::cout << "[INFO] 订阅者匹配断开: " << m_topic_name << std::endl;
        }
    }
    
    void onNewDataMessage(Subscriber* sub) override {
        MessageType msg;
        SampleInfo_t info;
        
        if (sub->takeNextData(&msg, &info)) {
            if (info.sampleKind == ALIVE) {
                m_message_count++;
                g_message_sequence++;
                
                // 处理消息
                handleMessage(msg);
            }
        }
    }
    
    uint64_t getMessageCount() const {
        return m_message_count.load();
    }
    
private:
    void handleMessage(const MessageType& msg);
};

// 特化处理不同消息类型
template<>
void MessageSubscriberListener<remote_msgs::HandshakeRequest>::handleMessage(const remote_msgs::HandshakeRequest& msg) {
    g_handshake_request_count++;
    
    std::ostringstream oss;
    oss << "接收HandshakeRequest - noa_active_request=" << msg.noa_active_request() 
        << ", remote_override_status=" << msg.remote_override_status()
        << ", remote_override_ready=" << msg.remote_override_ready()
        << ", timestamp=" << msg.timestamp();
    
    if (g_logger) {
        g_logger->logMessage("handshake_request", oss.str(), getCurrentTimestamp(), g_message_sequence);
    }
    
    std::cout << "[RECV] " << oss.str() << std::endl;
}

template<>
void MessageSubscriberListener<remote_msgs::HandshakeResponse>::handleMessage(const remote_msgs::HandshakeResponse& msg) {
    g_handshake_response_count++;
    
    std::ostringstream oss;
    oss << "接收HandshakeResponse - noa_active_response=" << msg.noa_active_response()
        << ", remote_override_response=" << msg.remote_override_response()
        << ", current_control_source=" << (msg.current_control_source() ? "true" : "false")
        << ", timestamp=" << msg.timestamp();
    
    if (g_logger) {
        g_logger->logMessage("handshake_response", oss.str(), getCurrentTimestamp(), g_message_sequence);
    }
    
    std::cout << "[RECV] " << oss.str() << std::endl;
}

template<>
void MessageSubscriberListener<remote_msgs::ControlCmd>::handleMessage(const remote_msgs::ControlCmd& msg) {
    g_control_cmd_count++;
    
    std::ostringstream oss;
    oss << "接收ControlCmd - steering_angle_enable=" << (msg.steering_angle_enable() ? "true" : "false")
        << ", steering_angle=" << msg.steering_angle()
        << ", target_acceleration_enable=" << (msg.target_acceleration_enable() ? "true" : "false")
        << ", target_acceleration=" << msg.target_acceleration()
        << ", gear_position_enable=" << (msg.gear_position_enable() ? "true" : "false")
        << ", gear_position=" << msg.gear_position()
        << ", timestamp=" << msg.timestamp();
    
    if (g_logger) {
        g_logger->logMessage("vehicle_control_cmd", oss.str(), getCurrentTimestamp(), g_message_sequence);
    }
    
    std::cout << "[RECV] " << oss.str() << std::endl;
}

template<>
void MessageSubscriberListener<remote_msgs::VehicleStatus>::handleMessage(const remote_msgs::VehicleStatus& msg) {
    g_vehicle_status_count++;
    
    std::ostringstream oss;
    oss << "接收VehicleStatus - vehicle_id=" << msg.vehicle_id()
        << ", control_mode=" << msg.control_mode()
        << ", speed=" << msg.speed()
        << ", gear_position=" << msg.gear_position()
        << ", steering_angle=" << msg.steering_angle()
        << ", timestamp=" << msg.timestamp();
    
    if (g_logger) {
        g_logger->logMessage("vehicle_vehicle_status", oss.str(), getCurrentTimestamp(), g_message_sequence);
    }
    
    std::cout << "[RECV] " << oss.str() << std::endl;
}

// J6车机接收器类
class J6VehicleReceiver {
private:
    Participant* mp_participant;
    
    Subscriber* mp_handshake_req_subscriber;
    Subscriber* mp_handshake_resp_subscriber;
    Subscriber* mp_control_subscriber;
    Subscriber* mp_status_subscriber;
    
    remote_msgs::HandshakeRequestPubSubType m_handshake_req_type;
    remote_msgs::HandshakeResponsePubSubType m_handshake_resp_type;
    remote_msgs::ControlCmdPubSubType m_control_type;
    remote_msgs::VehicleStatusPubSubType m_status_type;
    
    std::unique_ptr<MessageSubscriberListener<remote_msgs::HandshakeRequest>> m_handshake_req_listener;
    std::unique_ptr<MessageSubscriberListener<remote_msgs::HandshakeResponse>> m_handshake_resp_listener;
    std::unique_ptr<MessageSubscriberListener<remote_msgs::ControlCmd>> m_control_listener;
    std::unique_ptr<MessageSubscriberListener<remote_msgs::VehicleStatus>> m_status_listener;
    
public:
    J6VehicleReceiver() : mp_participant(nullptr),
                          mp_handshake_req_subscriber(nullptr),
                          mp_handshake_resp_subscriber(nullptr),
                          mp_control_subscriber(nullptr),
                          mp_status_subscriber(nullptr) {}
    
    ~J6VehicleReceiver() {
        cleanup();
    }
    
    bool init() {
        std::cout << "=== J6车机接收器 (FastRTPS 2.6.10) ===" << std::endl;
        std::cout << "初始化车机接收器..." << std::endl;
        
        // 创建参与者
        ParticipantAttributes PParam;
        PParam.rtps.builtin.discovery_config.discoveryProtocol = DiscoveryProtocol_t::SIMPLE;
        PParam.rtps.builtin.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol = true;
        PParam.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationReaderANDSubscriptionWriter = true;
        PParam.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationWriterANDSubscriptionReader = true;
        
        if (g_config) {
            auto fastdds_config = g_config->getFastDDSConfig("vehicle_receiver");
            PParam.rtps.setName(fastdds_config.participant_name.c_str());
        } else {
            PParam.rtps.setName("J6VehicleReceiver");
        }
        
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
        
        // 创建监听器
        m_handshake_req_listener = std::make_unique<MessageSubscriberListener<remote_msgs::HandshakeRequest>>("handshake_request");
        m_handshake_resp_listener = std::make_unique<MessageSubscriberListener<remote_msgs::HandshakeResponse>>("handshake_response");
        m_control_listener = std::make_unique<MessageSubscriberListener<remote_msgs::ControlCmd>>("vehicle_control_cmd");
        m_status_listener = std::make_unique<MessageSubscriberListener<remote_msgs::VehicleStatus>>("vehicle_vehicle_status");
        
        // 创建订阅者
        if (!createHandshakeRequestSubscriber()) return false;
        if (!createHandshakeResponseSubscriber()) return false;
        if (!createControlSubscriber()) return false;
        if (!createStatusSubscriber()) return false;
        
        std::cout << "[SUCCESS] J6车机接收器初始化完成!" << std::endl;
        std::cout << "[INFO] 开始接收控制消息..." << std::endl;
        std::cout << "[INFO] 按Ctrl+C停止程序" << std::endl;
        return true;
    }
    
    void run() {
        auto start_time = std::chrono::steady_clock::now();
        int stats_interval = 5;
        
        if (g_config) {
            stats_interval = g_config->getInt("vehicle_receiver.statistics_interval_sec", 5);
        }
        
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(stats_interval));
            
            if (g_running) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time);
                    
                uint64_t total_received = g_handshake_request_count + g_handshake_response_count +
                                        g_control_cmd_count + g_vehicle_status_count;
                
                std::cout << "\n=== 接收统计 (运行时间: " << elapsed.count() << "s) ===" << std::endl;
                std::cout << "HandshakeRequest: " << g_handshake_request_count << " 条" << std::endl;
                std::cout << "HandshakeResponse: " << g_handshake_response_count << " 条" << std::endl;
                std::cout << "ControlCmd: " << g_control_cmd_count << " 条" << std::endl;
                std::cout << "VehicleStatus: " << g_vehicle_status_count << " 条" << std::endl;
                std::cout << "总计: " << total_received << " 条" << std::endl;
                std::cout << "日志记录: " << (g_logger ? g_logger->getTotalMessages() : 0) << " 条" << std::endl;
                std::cout << "==============================\n" << std::endl;
            }
        }
        
        std::cout << "[INFO] 程序结束" << std::endl;
    }
    
private:
    bool createHandshakeRequestSubscriber() {
        SubscriberAttributes Rparam;
        Rparam.topic.topicKind = NO_KEY;
        Rparam.topic.topicDataType = "remote_msgs::HandshakeRequest";
        Rparam.topic.topicName = "handshake/request";
        Rparam.topic.historyQos.kind = KEEP_LAST_HISTORY_QOS;
        Rparam.topic.historyQos.depth = 10;
        Rparam.qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;
        
        mp_handshake_req_subscriber = Domain::createSubscriber(mp_participant, Rparam, m_handshake_req_listener.get());
        return mp_handshake_req_subscriber != nullptr;
    }
    
    bool createHandshakeResponseSubscriber() {
        SubscriberAttributes Rparam;
        Rparam.topic.topicKind = NO_KEY;
        Rparam.topic.topicDataType = "remote_msgs::HandshakeResponse";
        Rparam.topic.topicName = "handshake/response";
        Rparam.topic.historyQos.kind = KEEP_LAST_HISTORY_QOS;
        Rparam.topic.historyQos.depth = 10;
        Rparam.qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;
        
        mp_handshake_resp_subscriber = Domain::createSubscriber(mp_participant, Rparam, m_handshake_resp_listener.get());
        return mp_handshake_resp_subscriber != nullptr;
    }
    
    bool createControlSubscriber() {
        SubscriberAttributes Rparam;
        Rparam.topic.topicKind = NO_KEY;
        Rparam.topic.topicDataType = "remote_msgs::ControlCmd";
        Rparam.topic.topicName = "vehicle/control_cmd";
        Rparam.topic.historyQos.kind = KEEP_LAST_HISTORY_QOS;
        Rparam.topic.historyQos.depth = 10;
        Rparam.qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;
        
        mp_control_subscriber = Domain::createSubscriber(mp_participant, Rparam, m_control_listener.get());
        return mp_control_subscriber != nullptr;
    }
    
    bool createStatusSubscriber() {
        SubscriberAttributes Rparam;
        Rparam.topic.topicKind = NO_KEY;
        Rparam.topic.topicDataType = "remote_msgs::VehicleStatus";
        Rparam.topic.topicName = "vehicle/vehicle_status";
        Rparam.topic.historyQos.kind = KEEP_LAST_HISTORY_QOS;
        Rparam.topic.historyQos.depth = 10;
        Rparam.qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;
        
        mp_status_subscriber = Domain::createSubscriber(mp_participant, Rparam, m_status_listener.get());
        return mp_status_subscriber != nullptr;
    }
    
    void cleanup() {
        if (mp_handshake_req_subscriber) {
            Domain::removeSubscriber(mp_handshake_req_subscriber);
        }
        if (mp_handshake_resp_subscriber) {
            Domain::removeSubscriber(mp_handshake_resp_subscriber);
        }
        if (mp_control_subscriber) {
            Domain::removeSubscriber(mp_control_subscriber);
        }
        if (mp_status_subscriber) {
            Domain::removeSubscriber(mp_status_subscriber);
        }
        if (mp_participant) {
            Domain::removeParticipant(mp_participant);
        }
    }
};

void signal_handler(int signal) {
    std::cout << "\n[INFO] 接收到退出信号，正在安全关闭..." << std::endl;
    g_running = false;
}

void cleanup() {
    if (g_logger) {
        g_logger->shutdown();
        g_logger.reset();
    }
    std::cout << "清理完成" << std::endl;
}

int main(int argc, char* argv[]) {
    // 设置信号处理
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
    
    J6VehicleReceiver receiver;
    
    if (receiver.init()) {
        receiver.run();
    }
    
    cleanup();
    return 0;
}
