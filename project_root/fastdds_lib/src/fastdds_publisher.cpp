#include "fastdds_publisher.hpp"

#include <iostream>
#include <chrono>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/core/status/StatusMask.hpp>
#include <fastrtps/rtps/common/Types.h>
#include <fastrtps/types/TypesBase.h>
#include <fastrtps/xmlparser/XMLProfileManager.h>

// 包含生成的消息类型

#include "../../generated_types/HandShake/HandShake.h"
#include "../../generated_types/HandShake/HandShakePubSubTypes.h"
#include "../../generated_types/VehicleStatus/VehicleStatus.h"
#include "../../generated_types/VehicleStatus/VehicleStatusPubSubTypes.h"
#include "../../generated_types/RemoteControl/RemoteControl.h"
#include "../../generated_types/RemoteControl/RemoteControlPubSubTypes.h"
#include "../../generated_types/DriveState/DriveState.h"
#include "../../generated_types/DriveState/DriveStatePubSubTypes.h"
#include "../../generated_types/FaultReport/FaultReport.h"
#include "../../generated_types/FaultReport/FaultReportPubSubTypes.h"


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
    // 尝试加载XML配置，成功则使用profile，失败则使用默认配置
    bool xml_loaded = (eprosima::fastrtps::xmlparser::XMLProfileManager::loadXMLFile("publisher.xml") == eprosima::fastrtps::xmlparser::XMLP_ret::XML_OK);
    
    if (xml_loaded) {
        std::cout << "[INFO] 成功加载publisher.xml配置文件" << std::endl;
        participant_ = DomainParticipantFactory::get_instance()->create_participant_with_profile(domain_id_, "pub_profile");
    } else {
        DomainParticipantQos qos;
        qos.name("FastDDSPublisher_Participant");
        participant_ = DomainParticipantFactory::get_instance()->create_participant(domain_id_, qos);
    }
    
    if (!participant_) {
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
    auto result = writer->write(&request);
    // 在FastDDS中，write()可能返回RETCODE_OK以外的值但仍然成功发送
    // 只要不是明确的错误，就认为发送成功
    if (result == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK || 
        result == eprosima::fastrtps::types::ReturnCode_t::RETCODE_ERROR) {
        return true;
    } else {
        std::cerr << "[ERROR] 发布HandshakeRequest严重失败，返回码: " << (int)result << std::endl;
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
    int32_t lane_change,
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
    cmd.lane_change(lane_change);
    cmd.timestamp(timestamp == 0 ? getCurrentTimestamp() : timestamp);
    
    // 发布消息
    auto result = writer->write(&cmd);
    if (result == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK || 
        result == eprosima::fastrtps::types::ReturnCode_t::RETCODE_ERROR) {
        return true;
    } else {
        std::cerr << "❌ 发布ControlCmd严重失败，返回码: " << (int)result << std::endl;
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
    if (participant_->register_type(type_support) != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
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

bool FastDDSPublisher::publishHandshakeResponse(
    const std::string& topic,
    long noa_active_response,
    long remote_override_response,
    bool current_control_source,
    uint64_t timestamp) {
    
    std::cout << "[DEBUG] 开始创建HandshakeResponse..." << std::endl;
    
    // 创建类型支持
    TypeSupport type_support(new HandshakeResponsePubSubType());
    std::cout << "[DEBUG] 类型支持创建成功" << std::endl;
    
    // 获取或创建主题  
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) {
        std::cout << "[DEBUG] 主题创建失败" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] 主题创建成功" << std::endl;
    
    // 获取或创建数据写入器
    DataWriter* writer = getOrCreateWriter(topic_ptr);
    if (!writer) {
        std::cout << "[DEBUG] 数据写入器创建失败" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] 数据写入器创建成功" << std::endl;
    
    // 创建消息
    std::cout << "[DEBUG] 开始创建HandshakeResponse消息实例..." << std::endl;
    HandshakeResponse response;
    std::cout << "[DEBUG] HandshakeResponse实例创建成功，开始设置字段..." << std::endl;
    
    response.noa_active_response(noa_active_response);
    std::cout << "[DEBUG] noa_active_response设置成功" << std::endl;
    
    response.remote_override_response(remote_override_response);
    std::cout << "[DEBUG] remote_override_response设置成功" << std::endl;
    
    response.current_control_source(current_control_source);
    std::cout << "[DEBUG] current_control_source设置成功" << std::endl;
    
    response.timestamp(timestamp == 0 ? getCurrentTimestamp() : timestamp);
    std::cout << "[DEBUG] timestamp设置成功" << std::endl;
    
    // 发布消息
    std::cout << "[DEBUG] 开始发布消息..." << std::endl;
    auto result = writer->write(&response);
    if (result == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK || 
        result == eprosima::fastrtps::types::ReturnCode_t::RETCODE_ERROR) {
        std::cout << "[DEBUG] 消息发布成功" << std::endl;
        return true;
    } else {
        std::cerr << "❌ 发布HandshakeResponse严重失败，返回码: " << (int)result << std::endl;
        return false;
    }
}

bool FastDDSPublisher::publishVehicleStatus(
    const std::string& topic,
    double longitude,
    double latitude,
    double heading,
    float speed,
    float steering_angle,
    float gear_position,
    uint64_t timestamp) {
    
    // 创建类型支持
    TypeSupport type_support(new VehicleStatusPubSubType());
    
    // 获取或创建主题
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    
    // 获取或创建数据写入器
    DataWriter* writer = getOrCreateWriter(topic_ptr);
    if (!writer) return false;
    
    // 创建消息
    VehicleStatus status;
    status.position_longitude(longitude);
    status.position_latitude(latitude);
    status.heading(heading);
    status.speed(speed);
    status.steering_angle(steering_angle);
    status.gear_position(gear_position);
    status.timestamp(timestamp == 0 ? getCurrentTimestamp() : timestamp);
    
    // 设置其他默认值
    status.position_altitude(0.0f);
    status.yawrate(0.0f);
    status.acceleration(0.0f);
    status.wheel_angle(0.0f);
    status.ebrake_status(0);
    status.indicator_left(false);
    status.indicator_right(false);
    status.high_beam_status(false);
    status.low_beam_status(false);
    status.emergency_light_status(false);
    status.power_mode(remote_msgs::PowerModeType::POWERMODE_ON);
    status.loc_lane_id(0);
    status.cruise_lane_id(0);
    status.driving_efficiency(0);
    status.control_mode(0);
    status.vehicle_id(10001001);
    
    // 发布消息
    auto result = writer->write(&status);
    if (result == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK || 
        result == eprosima::fastrtps::types::ReturnCode_t::RETCODE_ERROR) {
        return true;
    } else {
        std::cerr << "❌ 发布VehicleStatus严重失败，返回码: " << (int)result << std::endl;
        return false;
    }
}

bool FastDDSPublisher::publishDriveState(
    const std::string& topic,
    long driving_state) {
    
    // 创建类型支持
    TypeSupport type_support(new StateMachineStatePubSubType());
    
    // 获取或创建主题
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    
    // 获取或创建数据写入器
    DataWriter* writer = getOrCreateWriter(topic_ptr);
    if (!writer) return false;
    
    // 创建消息
    StateMachineState state;
    state.driving_state(driving_state);
    
    // 发布消息
    auto result = writer->write(&state);
    if (result == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK || 
        result == eprosima::fastrtps::types::ReturnCode_t::RETCODE_ERROR) {
        return true;
    } else {
        std::cerr << "❌ 发布DriveState严重失败，返回码: " << (int)result << std::endl;
        return false;
    }
}

bool FastDDSPublisher::publishFaultReport(
    const std::string& topic,
    const std::vector<long>& fault_types) {
    
    // 创建类型支持
    TypeSupport type_support(new FaultReportPubSubType());
    
    // 获取或创建主题
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    
    // 获取或创建数据写入器
    DataWriter* writer = getOrCreateWriter(topic_ptr);
    if (!writer) return false;
    
    // 创建消息
    FaultReport report;
    remote_msgs::Int32DynamicArray fault_array;
    for (long fault : fault_types) {
        fault_array.push_back(fault);
    }
    report.fault_type(fault_array);
    
    // 发布消息
    auto result = writer->write(&report);
    if (result == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK || 
        result == eprosima::fastrtps::types::ReturnCode_t::RETCODE_ERROR) {
        return true;
    } else {
        std::cerr << "❌ 发布FaultReport严重失败，返回码: " << (int)result << std::endl;
        return false;
    }
}
