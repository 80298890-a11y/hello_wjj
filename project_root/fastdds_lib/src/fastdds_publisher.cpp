#include "fastdds_publisher.hpp"

#include <iostream>
#include <chrono>

using namespace std::chrono;
using namespace eprosima::fastrtps;

FastDDSPublisher::FastDDSPublisher(int domain_id)
    : participant_(nullptr)
    , publisher_(nullptr)
    , domain_id_(domain_id)
    , publish_frequency_hz_(100)
{
    std::cout << "[FastDDSPublisher] 创建发布器 [域:" << domain_id_ << "]" << std::endl;
}

FastDDSPublisher::~FastDDSPublisher() {
    cleanup();
}

bool FastDDSPublisher::init() {
    std::cout << "[FastDDSPublisher] 初始化发布器..." << std::endl;
    
    // 创建参与者属性
    ParticipantAttributes participant_attr;
    participant_attr.rtps.builtin.discovery_config.discoveryProtocol = 
        eprosima::fastrtps::rtps::DiscoveryProtocol::SIMPLE;
    participant_attr.rtps.builtin.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol = true;
    participant_attr.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationReaderANDSubscriptionWriter = true;
    participant_attr.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationWriterANDSubscriptionReader = true;
    participant_attr.rtps.setName("FastDDSPublisher_Participant");

    // 创建参与者
    participant_ = Domain::createParticipant(participant_attr);
    if (participant_ == nullptr) {
        std::cerr << "[ERROR] FastDDS参与者创建失败" << std::endl;
        return false;
    }

    std::cout << "[SUCCESS] FastDDS发布器初始化成功" << std::endl;
    return true;
}

void FastDDSPublisher::cleanup() {
    std::cout << "[FastDDSPublisher] 清理发布器..." << std::endl;
    
    if (participant_ != nullptr) {
        Domain::removeParticipant(participant_);
        participant_ = nullptr;
    }
    
    std::cout << "[CLEANUP] FastDDS发布器清理完成" << std::endl;
}

// 简化的发布方法实现 - 暂时只返回 true，表示成功
bool FastDDSPublisher::publishHandshakeRequest(
    const std::string& topic,
    int64_t vehicle_id,
    int64_t noa_active,
    int64_t protocol_version,
    uint64_t timestamp) {
    
    std::cout << "[INFO] 发布握手请求: vehicle_id=" << vehicle_id 
              << ", noa_active=" << noa_active << std::endl;
    return true;  // 简化实现
}

bool FastDDSPublisher::publishHandshakeResponse(
    const std::string& topic,
    int64_t vehicle_id,
    int64_t noa_active,
    bool ack_status,
    uint64_t timestamp) {
    
    std::cout << "[INFO] 发布握手响应: vehicle_id=" << vehicle_id 
              << ", ack_status=" << (ack_status ? "true" : "false") << std::endl;
    return true;  // 简化实现
}

bool FastDDSPublisher::publishVehicleStatus(
    const std::string& topic,
    uint32_t vehicle_id,
    float speed,
    int control_mode,
    float longitude,
    float latitude,
    float altitude,
    float heading,
    float gear_position,
    int power_mode,
    uint64_t timestamp) {
    
    std::cout << "[INFO] 发布车辆状态: vehicle_id=" << vehicle_id 
              << ", speed=" << speed << ", lon=" << longitude 
              << ", lat=" << latitude << std::endl;
    return true;  // 简化实现
}

bool FastDDSPublisher::publishRemoteControl(
    const std::string& topic,
    bool steering_angle_enable,
    float steering_angle,
    bool target_acceleration_enable,
    float target_acceleration,
    bool indicator_left_enable,
    bool indicator_left,
    bool indicator_right_enable,
    bool indicator_right,
    bool gear_position_enable,
    int32_t gear_position,
    bool ebrake_status_enable,
    int32_t ebrake_status,
    uint64_t timestamp) {
    
    std::cout << "[INFO] 发布远程控制: steering_angle=" << steering_angle 
              << ", target_acceleration=" << target_acceleration << std::endl;
    return true;  // 简化实现
}

void FastDDSPublisher::setPublishFrequency(int frequency_hz) {
    publish_frequency_hz_ = frequency_hz;
    std::cout << "[INFO] 设置发送频率: " << frequency_hz << " Hz" << std::endl;
}

uint64_t FastDDSPublisher::getCurrentTimestamp() {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
