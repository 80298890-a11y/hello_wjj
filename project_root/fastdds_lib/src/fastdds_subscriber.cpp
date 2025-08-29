#include "fastdds_subscriber.hpp"

#include <iostream>
#include <chrono>

using namespace std::chrono;
using namespace eprosima::fastrtps;

FastDDSSubscriber::FastDDSSubscriber(int domain_id)
    : participant_(nullptr)
    , subscriber_(nullptr)
    , domain_id_(domain_id)
{
    std::cout << "[FastDDSSubscriber] 创建订阅器 [域:" << domain_id_ << "]" << std::endl;
}

FastDDSSubscriber::~FastDDSSubscriber() {
    cleanup();
}

bool FastDDSSubscriber::init() {
    std::cout << "[FastDDSSubscriber] 初始化订阅器..." << std::endl;
    
    // 创建参与者属性
    ParticipantAttributes participant_attr;
    participant_attr.rtps.builtin.discovery_config.discoveryProtocol = 
        eprosima::fastrtps::rtps::DiscoveryProtocol::SIMPLE;
    participant_attr.rtps.builtin.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol = true;
    participant_attr.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationReaderANDSubscriptionWriter = true;
    participant_attr.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationWriterANDSubscriptionReader = true;
    participant_attr.rtps.setName("FastDDSSubscriber_Participant");

    // 创建参与者
    participant_ = Domain::createParticipant(participant_attr);
    if (participant_ == nullptr) {
        std::cerr << "[ERROR] FastDDS参与者创建失败" << std::endl;
        return false;
    }

    std::cout << "[SUCCESS] FastDDS订阅器初始化成功" << std::endl;
    return true;
}

void FastDDSSubscriber::cleanup() {
    std::cout << "[FastDDSSubscriber] 清理订阅器..." << std::endl;
    
    if (participant_ != nullptr) {
        Domain::removeParticipant(participant_);
        participant_ = nullptr;
    }
    
    std::cout << "[CLEANUP] FastDDS订阅器清理完成" << std::endl;
}

// 简化的回调注册方法 - 暂时只存储回调函数
void FastDDSSubscriber::setHandshakeRequestCallback(HandshakeRequestCallback callback) {
    handshake_request_callback_ = callback;
    std::cout << "[INFO] 注册握手请求回调函数" << std::endl;
}

void FastDDSSubscriber::setHandshakeResponseCallback(HandshakeResponseCallback callback) {
    handshake_response_callback_ = callback;
    std::cout << "[INFO] 注册握手响应回调函数" << std::endl;
}

void FastDDSSubscriber::setVehicleStatusCallback(VehicleStatusCallback callback) {
    vehicle_status_callback_ = callback;
    std::cout << "[INFO] 注册车辆状态回调函数" << std::endl;
}

void FastDDSSubscriber::setRemoteControlCallback(RemoteControlCallback callback) {
    remote_control_callback_ = callback;
    std::cout << "[INFO] 注册远程控制回调函数" << std::endl;
}

// 简化的开始监听方法
bool FastDDSSubscriber::startListening() {
    std::cout << "[INFO] 开始监听 FastDDS 消息..." << std::endl;
    return true;  // 简化实现
}

void FastDDSSubscriber::stopListening() {
    std::cout << "[INFO] 停止监听 FastDDS 消息" << std::endl;
}

// 其他方法的简化实现
bool FastDDSSubscriber::subscribeToHandshakeRequest(const std::string& topic) {
    std::cout << "[INFO] 订阅握手请求主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeToHandshakeResponse(const std::string& topic) {
    std::cout << "[INFO] 订阅握手响应主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeToVehicleStatus(const std::string& topic) {
    std::cout << "[INFO] 订阅车辆状态主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeToRemoteControl(const std::string& topic) {
    std::cout << "[INFO] 订阅远程控制主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeAllTopics() {
    std::cout << "[INFO] 订阅所有支持的主题" << std::endl;
    subscribeToHandshakeRequest("/handshake/request");
    subscribeToHandshakeResponse("/handshake/response");
    subscribeToVehicleStatus("/vehicle/vehicle_status");
    subscribeToRemoteControl("/vehicle/control_cmd");
    return true;
}
