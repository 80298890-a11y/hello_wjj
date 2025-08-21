#include "fastdds_publisher.hpp"

#include <iostream>
#include <chrono>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/core/status/StatusMask.hpp>
#include <fastdds/dds/core/ReturnCode.hpp>

// 包含生成的消息类型
#include "Handshake.hpp"
#include "HandshakePubSubTypes.hpp"
#include "HandshakeTypeObjectSupport.hpp"
#include "VehicleStatus.hpp"
#include "VehicleStatusPubSubTypes.hpp"
#include "VehicleStatusTypeObjectSupport.hpp"
#include "RemoteControl.hpp"
#include "RemoteControlPubSubTypes.hpp"
#include "RemoteControlTypeObjectSupport.hpp"


using namespace std::chrono;
using namespace eprosima::fastdds::dds;
using namespace remote_msgs;


FastDDSPublisher::FastDDSPublisher(int domain_id)
    : participant_(nullptr)
    , publisher_(nullptr)
    , domain_id_(domain_id)
    , publish_frequency_hz_(100)
{
}

FastDDSPublisher::~FastDDSPublisher() {
    cleanup();
}

bool FastDDSPublisher::init() {
    // 创建参与者
    DomainParticipantQos participant_qos;
    participant_qos.name("FastDDSPublisher_Participant");
    
    participant_ = DomainParticipantFactory::get_instance()->create_participant(
        domain_id_, participant_qos);
    
    if (participant_ == nullptr) {
        std::cerr << "❌ 创建DDS参与者失败" << std::endl;
        return false;
    }
    
    // 创建发布者
    publisher_ = participant_->create_publisher(PUBLISHER_QOS_DEFAULT, nullptr);
    if (publisher_ == nullptr) {
        std::cerr << "❌ 创建DDS发布者失败" << std::endl;
        return false;
    }
    
    std::cout << "✅ FastDDS发布者初始化成功" << std::endl;
    return true;
}

void FastDDSPublisher::cleanup() {
    // 清理所有数据写入器
    for (auto& pair : writers_) {
        if (pair.second != nullptr) {
            publisher_->delete_datawriter(pair.second);
        }
    }
    writers_.clear();
    
    // 清理所有主题
    for (auto& pair : topics_) {
        if (pair.second != nullptr) {
            participant_->delete_topic(pair.second);
        }
    }
    topics_.clear();
    type_supports_.clear();
    
    // 清理发布者
    if (participant_ != nullptr && publisher_ != nullptr) {
        participant_->delete_publisher(publisher_);
        publisher_ = nullptr;
    }
    
    // 清理参与者
    if (participant_ != nullptr) {
        DomainParticipantFactory::get_instance()->delete_participant(participant_);
        participant_ = nullptr;
    }
}

bool FastDDSPublisher::publishHandshakeRequest(
    const std::string& topic,
    long noa_active,
    long override_status,
    long override_ready,
    uint64_t timestamp) {
    
    // 创建类型支持
    TypeSupport type_support(new HandshakeRequestPubSubType());
    
    // 获取或创建主题  
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    
    // 获取或创建数据写入器
    DataWriter* writer = getOrCreateWriter(topic_ptr);
    if (!writer) return false;
    
    // 创建消息
    HandshakeRequest request;
    request.noa_active_request(noa_active);
    request.remote_override_status(override_status);
    request.remote_override_ready(override_ready);
    request.timestamp(timestamp == 0 ? getCurrentTimestamp() : timestamp);
    
    // 发布消息
    if (writer->write(&request) == RETCODE_OK) {
        return true;
    } else {
        std::cerr << "❌ 发布HandshakeRequest失败" << std::endl;
        return false;
    }
}

bool FastDDSPublisher::publishHandshakeResponse(
    const std::string& topic,
    long noa_active,
    long override_response,
    bool current_control_source,
    uint64_t timestamp) {
    
    // 创建类型支持
    TypeSupport type_support(new HandshakeResponsePubSubType());
    
    // 获取或创建主题
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    
    // 获取或创建数据写入器
    DataWriter* writer = getOrCreateWriter(topic_ptr);
    if (!writer) return false;
    
    // 创建消息
    HandshakeResponse response;
    response.noa_active_response(noa_active);
    response.remote_override_response(override_response);
    response.current_control_source(current_control_source);
    response.timestamp(timestamp == 0 ? getCurrentTimestamp() : timestamp);
    
    // 发布消息
    if (writer->write(&response) == RETCODE_OK) {
        return true;
    } else {
        std::cerr << "❌ 发布HandshakeResponse失败" << std::endl;
        return false;
    }
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
    
    // 创建类型支持
    TypeSupport type_support(new VehicleStatusPubSubType());
    
    // 获取或创建主题
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    
    // 获取或创建数据写入器
    DataWriter* writer = getOrCreateWriter(topic_ptr);
    if (!writer) return false;
    
    // 创建消息 - 设置所有16个IDL字段
    VehicleStatus status;
    status.vehicle_id(vehicle_id);
    status.control_mode(static_cast<remote_msgs::ControlMode>(control_mode));
    status.position_longitude(longitude);
    status.position_latitude(latitude);
    status.position_altitude(altitude);
    status.speed(speed);
    status.yawrate(0.0f);  // 默认值
    status.gear_position(gear_position);
    status.acceleration(0.0f);  // 默认值
    status.heading(heading);
    status.steering_angle(0.0f);  // 默认值
    status.wheel_angle(0.0f);  // 默认值
    status.ebrake_status(0);  // 默认值
    status.indicator_left(0);  // 默认值
    status.indicator_right(0);  // 默认值
    status.timestamp(timestamp == 0 ? getCurrentTimestamp() : timestamp);
    status.power_mode(static_cast<remote_msgs::PowerModeType>(power_mode));
    
    // 发布消息
    if (writer->write(&status) == RETCODE_OK) {
        return true;
    } else {
        std::cerr << "❌ 发布VehicleStatus失败" << std::endl;
        return false;
    }
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
    
    // 创建类型支持
    TypeSupport type_support(new ControlCmdPubSubType());
    
    // 获取或创建主题
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    
    // 获取或创建数据写入器
    DataWriter* writer = getOrCreateWriter(topic_ptr);
    if (!writer) return false;
    
    // 创建消息
    ControlCmd cmd;
    cmd.steering_angle_enable(steering_angle_enable);
    cmd.steering_angle(steering_angle);
    cmd.target_acceleration_enable(target_acceleration_enable);
    cmd.target_acceleration(target_acceleration);
    cmd.indicator_left_enable(indicator_left_enable);
    cmd.indicator_left(indicator_left);
    cmd.indicator_right_enable(indicator_right_enable);
    cmd.indicator_right(indicator_right);
    cmd.gear_position_enable(gear_position_enable);
    cmd.gear_position(gear_position);
    cmd.ebrake_status_enable(ebrake_status_enable);
    cmd.ebrake_status(ebrake_status);
    // remote_drive_ready和remote_drive_takeover_request字段在IDL中被注释掉了
    cmd.timestamp(timestamp == 0 ? getCurrentTimestamp() : timestamp);
    
    // 发布消息
    if (writer->write(&cmd) == RETCODE_OK) {
        return true;
    } else {
        std::cerr << "❌ 发布ControlCmd失败" << std::endl;
        return false;
    }
}

void FastDDSPublisher::setPublishFrequency(int frequency_hz) {
    publish_frequency_hz_ = frequency_hz;
}

uint64_t FastDDSPublisher::getCurrentTimestamp() {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

Topic* FastDDSPublisher::getOrCreateTopic(const std::string& topic_name,
                                         const std::string& type_name,
                                         TypeSupport type_support) {
    // 检查缓存
    auto it = topics_.find(topic_name);
    if (it != topics_.end()) {
        return it->second;
    }
    
    // 注册类型
    if (participant_->register_type(type_support) != RETCODE_OK) {
        std::cerr << "❌ 注册类型 " << type_name << " 失败" << std::endl;
        return nullptr;
    }
    
    // 创建主题
    Topic* topic = participant_->create_topic(topic_name, type_name, TOPIC_QOS_DEFAULT);
    if (topic == nullptr) {
        std::cerr << "❌ 创建主题 " << topic_name << " 失败" << std::endl;
        return nullptr;
    }
    
    // 缓存
    topics_[topic_name] = topic;
    type_supports_[topic_name] = type_support;
    
    return topic;
}

DataWriter* FastDDSPublisher::getOrCreateWriter(Topic* topic) {
    std::string topic_name = topic->get_name();
    
    // 检查缓存
    auto it = writers_.find(topic_name);
    if (it != writers_.end()) {
        return it->second;
    }
    
    // 配置QoS
    DataWriterQos writer_qos;
    writer_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
    // 禁用数据共享以避免共享内存问题
    writer_qos.data_sharing().off();
    
    // 创建数据写入器
    DataWriter* writer = publisher_->create_datawriter(topic, writer_qos);
    if (writer == nullptr) {
        std::cerr << "❌ 创建数据写入器失败 for topic: " << topic_name << std::endl;
        return nullptr;
    }
    
    // 缓存
    writers_[topic_name] = writer;
    
    return writer;
}
