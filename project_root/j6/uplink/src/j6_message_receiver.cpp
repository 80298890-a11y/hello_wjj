#include <fastrtps/participant/Participant.h>
#include <fastrtps/attributes/ParticipantAttributes.h>
#include <fastrtps/subscriber/Subscriber.h>
#include <fastrtps/attributes/SubscriberAttributes.h>
#include <fastrtps/subscriber/SubscriberListener.h>
#include <fastrtps/subscriber/SampleInfo.h>
#include <fastrtps/Domain.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <signal.h>
#include <sstream>

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

// 全局退出标志
std::atomic<bool> g_running{true};
std::unique_ptr<AsyncLogger> g_logger;

// 信号处理函数
void signalHandler(int signal) {
    std::cout << "\n[INFO] 接收到退出信号，正在安全关闭..." << std::endl;
    g_running = false;
}

// 消息序列号计数器
std::atomic<uint64_t> g_message_sequence{0};

// 通用的订阅者监听器模板
template<typename MessageType>
class MessageSubscriberListener : public SubscriberListener {
private:
    std::string m_topic_name;
    std::atomic<uint64_t> m_message_count{0};
    
public:
    MessageSubscriberListener(const std::string& topic_name) : m_topic_name(topic_name) {}
    
    void onSubscriptionMatched(Subscriber* sub, MatchingInfo& info) override {
        if (info.status == MATCHED_MATCHING) {
            std::cout << "[SUCCESS] " << m_topic_name << " 订阅者匹配到发布者!" << std::endl;
        } else {
            std::cout << "[INFO] " << m_topic_name << " 发布者断开连接!" << std::endl;
        }
    }
    
    void onNewDataMessage(Subscriber* sub) override {
        MessageType msg;
        SampleInfo_t sample_info;
        
        if (sub->takeNextData(&msg, &sample_info)) {
            if (sample_info.sampleKind == ALIVE) {
                uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                    
                uint64_t seq_id = g_message_sequence.fetch_add(1);
                
                // 将消息转换为字符串（这里简化处理，实际可以用JSON等格式）
                std::string message_data = messageToString(msg);
                
                // 异步记录日志
                if (g_logger) {
                    g_logger->logMessage(m_topic_name, message_data, now, seq_id);
                }
                
                m_message_count++;
                
                // 每100条消息打印一次接收状态
                if (m_message_count % 100 == 0) {
                    std::cout << "[" << m_topic_name << "] 已接收 " << m_message_count 
                             << " 条消息，队列大小: " << g_logger->getQueueSize() << std::endl;
                }
            }
        }
    }
    
    uint64_t getMessageCount() const { return m_message_count.load(); }
    
private:
    std::string messageToString(const remote_msgs::HandshakeRequest& msg) {
        std::stringstream ss;
        ss << "noa_active_request=" << msg.noa_active_request()
           << ",remote_override_status=" << msg.remote_override_status()
           << ",remote_override_ready=" << msg.remote_override_ready()
           << ",timestamp=" << msg.timestamp();
        return ss.str();
    }
    
    std::string messageToString(const remote_msgs::HandshakeResponse& msg) {
        std::stringstream ss;
        ss << "noa_active_response=" << msg.noa_active_response()
           << ",remote_override_response=" << msg.remote_override_response()
           << ",current_control_source=" << (msg.current_control_source() ? "true" : "false")
           << ",timestamp=" << msg.timestamp();
        return ss.str();
    }
    
    std::string messageToString(const remote_msgs::ControlCmd& msg) {
        std::stringstream ss;
        ss << "steering_angle_enable=" << (msg.steering_angle_enable() ? "true" : "false")
           << ",steering_angle=" << msg.steering_angle()
           << ",target_acceleration_enable=" << (msg.target_acceleration_enable() ? "true" : "false")
           << ",target_acceleration=" << msg.target_acceleration()
           << ",indicator_left_enable=" << (msg.indicator_left_enable() ? "true" : "false")
           << ",indicator_left=" << (msg.indicator_left() ? "true" : "false")
           << ",indicator_right_enable=" << (msg.indicator_right_enable() ? "true" : "false")
           << ",indicator_right=" << (msg.indicator_right() ? "true" : "false")
           << ",gear_position_enable=" << (msg.gear_position_enable() ? "true" : "false")
           << ",gear_position=" << msg.gear_position()
           << ",ebrake_status_enable=" << (msg.ebrake_status_enable() ? "true" : "false")
           << ",ebrake_status=" << msg.ebrake_status()
           << ",timestamp=" << msg.timestamp();
        return ss.str();
    }
    
    std::string messageToString(const remote_msgs::VehicleStatus& msg) {
        std::stringstream ss;
        ss << "vehicle_id=" << msg.vehicle_id()
           << ",control_mode=" << static_cast<int>(msg.control_mode())
           << ",position_longitude=" << msg.position_longitude()
           << ",position_latitude=" << msg.position_latitude()
           << ",position_altitude=" << msg.position_altitude()
           << ",speed=" << msg.speed()
           << ",yawrate=" << msg.yawrate()
           << ",gear_position=" << msg.gear_position()
           << ",acceleration=" << msg.acceleration()
           << ",heading=" << msg.heading()
           << ",steering_angle=" << msg.steering_angle()
           << ",wheel_angle=" << msg.wheel_angle()
           << ",ebrake_status=" << msg.ebrake_status()
           << ",indicator_left=" << msg.indicator_left()
           << ",indicator_right=" << msg.indicator_right()
           << ",timestamp=" << msg.timestamp()
           << ",power_mode=" << static_cast<int>(msg.power_mode());
        return ss.str();
    }
};

class J6MessageReceiver {
private:
    Participant* mp_participant;
    
    // 订阅者
    Subscriber* mp_handshake_req_subscriber;
    Subscriber* mp_handshake_resp_subscriber;
    Subscriber* mp_control_subscriber;
    Subscriber* mp_status_subscriber;
    
    // 类型支持
    remote_msgs::HandshakeRequestPubSubType m_handshake_req_type;
    remote_msgs::HandshakeResponsePubSubType m_handshake_resp_type;
    remote_msgs::ControlCmdPubSubType m_control_type;
    remote_msgs::VehicleStatusPubSubType m_status_type;
    
    // 监听器
    std::unique_ptr<MessageSubscriberListener<remote_msgs::HandshakeRequest>> m_handshake_req_listener;
    std::unique_ptr<MessageSubscriberListener<remote_msgs::HandshakeResponse>> m_handshake_resp_listener;
    std::unique_ptr<MessageSubscriberListener<remote_msgs::ControlCmd>> m_control_listener;
    std::unique_ptr<MessageSubscriberListener<remote_msgs::VehicleStatus>> m_status_listener;
    
public:
    J6MessageReceiver() 
        : mp_participant(nullptr)
        , mp_handshake_req_subscriber(nullptr)
        , mp_handshake_resp_subscriber(nullptr)
        , mp_control_subscriber(nullptr)
        , mp_status_subscriber(nullptr)
    {}
    
    ~J6MessageReceiver() {
        cleanup();
    }
    
    bool init() {
        std::cout << "=== J6 消息接收器 (FastRTPS 2.6.10) ===" << std::endl;
        std::cout << "初始化消息接收器..." << std::endl;
        
        // 初始化异步日志系统
        g_logger = std::make_unique<AsyncLogger>();
        if (!g_logger->init("/home/wjj/work/project_root/j6/uplink/logs")) {
            std::cerr << "[ERROR] 异步日志系统初始化失败!" << std::endl;
            return false;
        }
        
        // 创建参与者
        ParticipantAttributes PParam;
        PParam.rtps.builtin.discovery_config.discoveryProtocol = DiscoveryProtocol_t::SIMPLE;
        PParam.rtps.builtin.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol = true;
        PParam.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationReaderANDSubscriptionWriter = true;
        PParam.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationWriterANDSubscriptionReader = true;
        PParam.rtps.setName("J6MessageReceiver");
        
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
        
        std::cout << "[SUCCESS] J6消息接收器初始化完成!" << std::endl;
        std::cout << "[INFO] 开始接收消息并记录日志..." << std::endl;
        std::cout << "[INFO] 按Ctrl+C停止程序" << std::endl;
        return true;
    }
    
    void run() {
        auto start_time = std::chrono::steady_clock::now();
        
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            if (g_running) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time);
                    
                uint64_t total_received = m_handshake_req_listener->getMessageCount() +
                                        m_handshake_resp_listener->getMessageCount() +
                                        m_control_listener->getMessageCount() +
                                        m_status_listener->getMessageCount();
                
                std::cout << "[统计] 运行时间: " << elapsed.count() << "s, "
                         << "总接收: " << total_received << " 条, "
                         << "总记录: " << g_logger->getTotalMessages() << " 条, "
                         << "队列: " << g_logger->getQueueSize() << std::endl;
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
        
        // 关闭日志系统
        if (g_logger) {
            g_logger->shutdown();
            g_logger.reset();
        }
    }
};

int main() {
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    J6MessageReceiver receiver;
    
    if (receiver.init()) {
        receiver.run();
    }
    
    return 0;
}
