#include <fastrtps/participant/Participant.h>
#include <fastrtps/attributes/ParticipantAttributes.h>
#include <fastrtps/publisher/Publisher.h>
#include <fastrtps/attributes/PublisherAttributes.h>
#include <fastrtps/Domain.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <signal.h>

// 包含生成的IDL类型支持头文件
#include "Handshake.h"
#include "HandshakePubSubTypes.h"
#include "RemoteControl.h"
#include "RemoteControlPubSubTypes.h"
#include "VehicleStatus.h"
#include "VehicleStatusPubSubTypes.h"

using namespace eprosima::fastrtps;
using namespace eprosima::fastrtps::rtps;

// 全局退出标志
std::atomic<bool> g_running{true};

// 信号处理函数
void signalHandler(int signal) {
    std::cout << "\n[INFO] 接收到退出信号，正在安全关闭..." << std::endl;
    g_running = false;
}

class VehicleUplinkSimulator
{
private:
    Participant* mp_participant;
    
    // Handshake 发布者
    Publisher* mp_handshake_req_publisher;
    Publisher* mp_handshake_resp_publisher;
    remote_msgs::HandshakeRequestPubSubType m_handshake_req_type;
    remote_msgs::HandshakeResponsePubSubType m_handshake_resp_type;
    
    // RemoteControl 发布者
    Publisher* mp_control_publisher;
    remote_msgs::ControlCmdPubSubType m_control_type;
    
    // VehicleStatus 发布者
    Publisher* mp_status_publisher;
    remote_msgs::VehicleStatusPubSubType m_status_type;
    
    // 消息计数器
    uint64_t m_message_count;
    
public:
    VehicleUplinkSimulator() 
        : mp_participant(nullptr)
        , mp_handshake_req_publisher(nullptr)
        , mp_handshake_resp_publisher(nullptr)
        , mp_control_publisher(nullptr)
        , mp_status_publisher(nullptr)
        , m_message_count(0)
    {}
    
    ~VehicleUplinkSimulator()
    {
        cleanup();
    }
    
    bool init()
    {
        std::cout << "=== J6 车机上行模拟器 (FastRTPS 2.6.10) ===" << std::endl;
        std::cout << "初始化车机模拟器..." << std::endl;
        
        // 创建参与者
        ParticipantAttributes PParam;
        PParam.rtps.builtin.discovery_config.discoveryProtocol = DiscoveryProtocol_t::SIMPLE;
        PParam.rtps.builtin.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol = true;
        PParam.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationReaderANDSubscriptionWriter = true;
        PParam.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationWriterANDSubscriptionReader = true;
        PParam.rtps.setName("VehicleUplinkSimulator");
        
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
        
        // 创建Handshake Request发布者
        if (!createHandshakeRequestPublisher()) return false;
        
        // 创建Handshake Response发布者
        if (!createHandshakeResponsePublisher()) return false;
        
        // 创建RemoteControl发布者
        if (!createControlPublisher()) return false;
        
        // 创建VehicleStatus发布者
        if (!createStatusPublisher()) return false;
        
        std::cout << "[SUCCESS] 车机上行模拟器初始化完成!" << std::endl;
        std::cout << "[INFO] 发送频率: 100Hz" << std::endl;
        std::cout << "[INFO] 按Ctrl+C停止程序" << std::endl;
        return true;
    }
    
    void run()
    {
        auto start_time = std::chrono::steady_clock::now();
        const auto interval = std::chrono::milliseconds(10); // 100Hz = 10ms间隔
        
        while (g_running) {
            auto now = std::chrono::steady_clock::now();
            uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            
            // 发送Handshake Request
            sendHandshakeRequest(timestamp);
            
            // 发送Handshake Response  
            sendHandshakeResponse(timestamp);
            
            // 发送RemoteControl
            sendControlCommand(timestamp);
            
            // 发送VehicleStatus
            sendVehicleStatus(timestamp);
            
            m_message_count++;
            
            // 每1000条消息打印一次状态
            if (m_message_count % 1000 == 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time);
                std::cout << "[INFO] 已发送 " << m_message_count << " 轮消息，运行时间: " 
                         << elapsed.count() << "s (100Hz)" << std::endl;
            }
            
            // 精确控制发送频率
            std::this_thread::sleep_until(now + interval);
        }
        
        std::cout << "[INFO] 程序结束，总共发送了 " << m_message_count << " 轮消息" << std::endl;
    }
    
private:
    bool createHandshakeRequestPublisher()
    {
        PublisherAttributes Wparam;
        Wparam.topic.topicKind = NO_KEY;
        Wparam.topic.topicDataType = "remote_msgs::HandshakeRequest";
        Wparam.topic.topicName = "handshake/request";
        Wparam.topic.historyQos.kind = KEEP_LAST_HISTORY_QOS;
        Wparam.topic.historyQos.depth = 10;
        Wparam.qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;
        
        mp_handshake_req_publisher = Domain::createPublisher(mp_participant, Wparam, nullptr);
        return mp_handshake_req_publisher != nullptr;
    }
    
    bool createHandshakeResponsePublisher()
    {
        PublisherAttributes Wparam;
        Wparam.topic.topicKind = NO_KEY;
        Wparam.topic.topicDataType = "remote_msgs::HandshakeResponse";
        Wparam.topic.topicName = "handshake/response";
        Wparam.topic.historyQos.kind = KEEP_LAST_HISTORY_QOS;
        Wparam.topic.historyQos.depth = 10;
        Wparam.qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;
        
        mp_handshake_resp_publisher = Domain::createPublisher(mp_participant, Wparam, nullptr);
        return mp_handshake_resp_publisher != nullptr;
    }
    
    bool createControlPublisher()
    {
        PublisherAttributes Wparam;
        Wparam.topic.topicKind = NO_KEY;
        Wparam.topic.topicDataType = "remote_msgs::ControlCmd";
        Wparam.topic.topicName = "vehicle/control_cmd";
        Wparam.topic.historyQos.kind = KEEP_LAST_HISTORY_QOS;
        Wparam.topic.historyQos.depth = 10;
        Wparam.qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;
        
        mp_control_publisher = Domain::createPublisher(mp_participant, Wparam, nullptr);
        return mp_control_publisher != nullptr;
    }
    
    bool createStatusPublisher()
    {
        PublisherAttributes Wparam;
        Wparam.topic.topicKind = NO_KEY;
        Wparam.topic.topicDataType = "remote_msgs::VehicleStatus";
        Wparam.topic.topicName = "vehicle/vehicle_status";
        Wparam.topic.historyQos.kind = KEEP_LAST_HISTORY_QOS;
        Wparam.topic.historyQos.depth = 10;
        Wparam.qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;
        
        mp_status_publisher = Domain::createPublisher(mp_participant, Wparam, nullptr);
        return mp_status_publisher != nullptr;
    }
    
    void sendHandshakeRequest(uint64_t timestamp)
    {
        remote_msgs::HandshakeRequest msg;
        msg.noa_active_request(static_cast<int32_t>(m_message_count % 3 - 1)); // -1, 0, 1 循环
        msg.remote_override_status(static_cast<int32_t>(m_message_count % 3 - 1));
        msg.remote_override_ready(static_cast<int32_t>(m_message_count % 2));
        msg.timestamp(timestamp);
        
        mp_handshake_req_publisher->write(&msg);
    }
    
    void sendHandshakeResponse(uint64_t timestamp)
    {
        remote_msgs::HandshakeResponse msg;
        msg.noa_active_response(static_cast<int32_t>(m_message_count % 3 - 1));
        msg.remote_override_response(static_cast<int32_t>(m_message_count % 3 - 1));
        msg.current_control_source(m_message_count % 2 == 0);
        msg.timestamp(timestamp);
        
        mp_handshake_resp_publisher->write(&msg);
    }
    
    void sendControlCommand(uint64_t timestamp)
    {
        remote_msgs::ControlCmd msg;
        msg.steering_angle_enable(true);
        msg.steering_angle(sin(m_message_count * 0.01f) * 45.0f); // 模拟转向角
        msg.target_acceleration_enable(true);
        msg.target_acceleration(cos(m_message_count * 0.01f) * 2.0f); // 模拟加速度
        msg.indicator_left_enable(true);
        msg.indicator_left(m_message_count % 100 < 20);
        msg.indicator_right_enable(true);
        msg.indicator_right(m_message_count % 100 >= 20 && m_message_count % 100 < 40);
        msg.gear_position_enable(true);
        msg.gear_position(3); // D档
        msg.ebrake_status_enable(true);
        msg.ebrake_status(0); // 手刹释放
        msg.timestamp(timestamp);
        
        mp_control_publisher->write(&msg);
    }
    
    void sendVehicleStatus(uint64_t timestamp)
    {
        remote_msgs::VehicleStatus msg;
        msg.vehicle_id(12345678);
        msg.control_mode(static_cast<remote_msgs::ControlMode>(m_message_count % 3));
        msg.position_longitude(116.3974f + sin(m_message_count * 0.001f) * 0.001f);
        msg.position_latitude(39.9093f + cos(m_message_count * 0.001f) * 0.001f);
        msg.position_altitude(50.0f);
        msg.speed(60.0f + sin(m_message_count * 0.01f) * 10.0f);
        msg.yawrate(cos(m_message_count * 0.02f) * 5.0f);
        msg.gear_position(3.0f);
        msg.acceleration(cos(m_message_count * 0.01f) * 2.0f);
        msg.heading(fmod(m_message_count * 0.1f, 360.0f));
        msg.steering_angle(sin(m_message_count * 0.01f) * 45.0f);
        msg.wheel_angle(sin(m_message_count * 0.01f) * 35.0f);
        msg.ebrake_status(0);
        msg.indicator_left(m_message_count % 100 < 20 ? 1 : 0);
        msg.indicator_right(m_message_count % 100 >= 20 && m_message_count % 100 < 40 ? 1 : 0);
        msg.timestamp(timestamp);
        msg.power_mode(remote_msgs::PowerModeType::POWERMODE_ON);
        
        mp_status_publisher->write(&msg);
    }
    
    void cleanup()
    {
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
};

int main()
{
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    VehicleUplinkSimulator simulator;
    
    if (simulator.init()) {
        simulator.run();
    }
    
    return 0;
}

