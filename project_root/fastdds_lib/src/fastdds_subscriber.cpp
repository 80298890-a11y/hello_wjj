#include "fastdds_subscriber.hpp"

#include <iostream>
#include <chrono>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/core/status/StatusMask.hpp>
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


// =========================== 监听器类实现 ===========================

class FastDDSSubscriber::HandshakeRequestListener : public DataReaderListener {
private:
    FastDDSSubscriber* subscriber_;

public:
    HandshakeRequestListener(FastDDSSubscriber* subscriber) : subscriber_(subscriber) {}

    void on_data_available(DataReader* reader) override {
        SampleInfo info;
        HandshakeRequest request;
        
        if (reader->take_next_sample(&request, &info) == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
            if (info.valid_data && subscriber_->handshake_request_callback_) {
                subscriber_->handshake_request_callback_(
                    request.noa_active_request(),
                    request.remote_override_status(),
                    request.remote_override_ready(),
                    request.timestamp()
                );
            }
        }
    }
};

class FastDDSSubscriber::HandshakeResponseListener : public DataReaderListener {
private:
    FastDDSSubscriber* subscriber_;

public:
    HandshakeResponseListener(FastDDSSubscriber* subscriber) : subscriber_(subscriber) {}

    void on_data_available(DataReader* reader) override {
        SampleInfo info;
        HandshakeResponse response;
        
        if (reader->take_next_sample(&response, &info) == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
            if (info.valid_data && subscriber_->handshake_response_callback_) {
                subscriber_->handshake_response_callback_(
                    response.noa_active_response(),
                    response.remote_override_response(),
                    response.current_control_source(),
                    response.timestamp()
                );
            }
        }
    }
};

class FastDDSSubscriber::VehicleStatusListener : public DataReaderListener {
private:
    FastDDSSubscriber* subscriber_;

public:
    VehicleStatusListener(FastDDSSubscriber* subscriber) : subscriber_(subscriber) {}

    void on_data_available(DataReader* reader) override {
        SampleInfo info;
        VehicleStatus status;
        
        if (reader->take_next_sample(&status, &info) == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
            if (info.valid_data && subscriber_->vehicle_status_callback_) {
                subscriber_->vehicle_status_callback_(
                    status.vehicle_id(),
                    status.control_mode(),
                    status.position_longitude(),
                    status.position_latitude(),
                    status.position_altitude(),
                    status.speed(),
                    status.yawrate(),
                    status.gear_position(),
                    status.acceleration(),
                    status.heading(),
                    status.steering_angle(),
                    status.wheel_angle(),
                    status.ebrake_status(),
                    status.indicator_left(),
                    status.indicator_right(),
                    status.timestamp(),
                    static_cast<int>(status.power_mode())
                );
            }
        }
    }
};

class FastDDSSubscriber::RemoteControlListener : public DataReaderListener {
private:
    FastDDSSubscriber* subscriber_;

public:
    RemoteControlListener(FastDDSSubscriber* subscriber) : subscriber_(subscriber) {}

    void on_data_available(DataReader* reader) override {
        SampleInfo info;
        ControlCmd control;
        
        if (reader->take_next_sample(&control, &info) == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
            if (info.valid_data && subscriber_->remote_control_callback_) {
                subscriber_->remote_control_callback_(
                    control.steering_angle_enable(),
                    control.steering_angle(),
                    control.target_acceleration_enable(),
                    control.target_acceleration(),
                    control.indicator_left_enable(),
                    control.indicator_left(),
                    control.indicator_right_enable(),
                    control.indicator_right(),
                    control.gear_position_enable(),
                    control.gear_position(),
                    control.ebrake_status_enable(),
                    control.ebrake_status(),
                    control.timestamp()
                );
            }
        }
    }
};

class FastDDSSubscriber::DriveStateListener : public DataReaderListener {
private:
    FastDDSSubscriber* subscriber_;

public:
    DriveStateListener(FastDDSSubscriber* subscriber) : subscriber_(subscriber) {}

    void on_data_available(DataReader* reader) override {
        SampleInfo info;
        StateMachineState drive_state;
        
        if (reader->take_next_sample(&drive_state, &info) == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
            if (info.valid_data && subscriber_->drive_state_callback_) {
                // 当前时间戳，因为DriveState IDL没有timestamp字段
                uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                subscriber_->drive_state_callback_(
                    drive_state.driving_state(),
                    timestamp
                );
            }
        }
    }
};

class FastDDSSubscriber::FaultReportListener : public DataReaderListener {
private:
    FastDDSSubscriber* subscriber_;

public:
    FaultReportListener(FastDDSSubscriber* subscriber) : subscriber_(subscriber) {}

    void on_data_available(DataReader* reader) override {
        SampleInfo info;
        FaultReport fault_report;
        
        if (reader->take_next_sample(&fault_report, &info) == eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
            if (info.valid_data && subscriber_->fault_report_callback_) {
                // 当前时间戳，因为FaultReport IDL没有timestamp字段
                uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                subscriber_->fault_report_callback_(
                    fault_report.fault_type(),
                    timestamp
                );
            }
        }
    }
};


// FastDDSSubscriber实现

FastDDSSubscriber::FastDDSSubscriber(int domain_id)
    : participant_(nullptr)
    , subscriber_(nullptr)
    , domain_id_(domain_id)
{
}

FastDDSSubscriber::~FastDDSSubscriber() {
    cleanup();
}

bool FastDDSSubscriber::init() {
    // 尝试加载XML配置，成功则使用profile，失败则使用默认配置
    bool xml_loaded = (eprosima::fastrtps::xmlparser::XMLProfileManager::loadXMLFile("subscriber.xml") == eprosima::fastrtps::xmlparser::XMLP_ret::XML_OK);
    
    if (xml_loaded) {
        std::cout << "[INFO] 成功加载subscriber.xml配置文件" << std::endl;
        participant_ = DomainParticipantFactory::get_instance()->create_participant_with_profile(domain_id_, "sub_profile");
    } else {
        DomainParticipantQos qos;
        qos.name("FastDDSSubscriber_Participant");
        participant_ = DomainParticipantFactory::get_instance()->create_participant(domain_id_, qos);
    }
    
    if (!participant_) {
        std::cerr << "❌ 创建DDS订阅参与者失败" << std::endl;
        return false;
    }
    
    // 创建订阅者
    subscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT, nullptr);
    if (subscriber_ == nullptr) {
        std::cerr << "❌ 创建DDS订阅者失败" << std::endl;
        return false;
    }
    
    std::cout << "✅ FastDDS订阅者初始化成功" << std::endl;
    
    // 自动订阅所有主题
    return subscribeAllTopics();
}

void FastDDSSubscriber::cleanup() {
    // 清理所有监听器
    listeners_.clear();
    
    // 清理所有数据读取器
    for (auto& pair : readers_) {
        if (pair.second != nullptr) {
            subscriber_->delete_datareader(pair.second);
        }
    }
    readers_.clear();
    
    // 清理所有主题
    for (auto& pair : topics_) {
        if (pair.second != nullptr) {
            participant_->delete_topic(pair.second);
        }
    }
    topics_.clear();
    type_supports_.clear();
    
    // 清理订阅者
    if (participant_ != nullptr && subscriber_ != nullptr) {
        participant_->delete_subscriber(subscriber_);
        subscriber_ = nullptr;
    }
    
    // 清理参与者
    if (participant_ != nullptr) {
        DomainParticipantFactory::get_instance()->delete_participant(participant_);
        participant_ = nullptr;
    }
}

// 回调函数设置
void FastDDSSubscriber::setHandshakeRequestCallback(HandshakeRequestCallback callback) { handshake_request_callback_ = callback; }
void FastDDSSubscriber::setHandshakeResponseCallback(HandshakeResponseCallback callback) { handshake_response_callback_ = callback; }
void FastDDSSubscriber::setVehicleStatusCallback(VehicleStatusCallback callback) { vehicle_status_callback_ = callback; }
void FastDDSSubscriber::setRemoteControlCallback(RemoteControlCallback callback) { remote_control_callback_ = callback; }
void FastDDSSubscriber::setDriveStateCallback(DriveStateCallback callback) { drive_state_callback_ = callback; }
void FastDDSSubscriber::setFaultReportCallback(FaultReportCallback callback) { fault_report_callback_ = callback; }

// 订阅功能

bool FastDDSSubscriber::subscribeHandshakeRequest(const std::string& topic) {
    TypeSupport type_support(new HandshakeRequestPubSubType());
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    auto listener = std::make_unique<HandshakeRequestListener>(this);
    DataReader* reader = getOrCreateReader(topic_ptr, listener.get());
    if (!reader) return false;
    listeners_[topic] = std::move(listener);
    std::cout << "✅ 订阅HandshakeRequest主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeHandshakeResponse(const std::string& topic) {
    TypeSupport type_support(new HandshakeResponsePubSubType());
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    auto listener = std::make_unique<HandshakeResponseListener>(this);
    DataReader* reader = getOrCreateReader(topic_ptr, listener.get());
    if (!reader) return false;
    listeners_[topic] = std::move(listener);
    std::cout << "✅ 订阅HandshakeResponse主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeVehicleStatus(const std::string& topic) {
    TypeSupport type_support(new VehicleStatusPubSubType());
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    auto listener = std::make_unique<VehicleStatusListener>(this);
    DataReader* reader = getOrCreateReader(topic_ptr, listener.get());
    if (!reader) return false;
    listeners_[topic] = std::move(listener);
    std::cout << "✅ 订阅VehicleStatus主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeRemoteControl(const std::string& topic) {
    TypeSupport type_support(new ControlCmdPubSubType());
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    auto listener = std::make_unique<RemoteControlListener>(this);
    DataReader* reader = getOrCreateReader(topic_ptr, listener.get());
    if (!reader) return false;
    listeners_[topic] = std::move(listener);
    std::cout << "✅ 订阅RemoteControl主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeDriveState(const std::string& topic) {
    TypeSupport type_support(new StateMachineStatePubSubType());
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    auto listener = std::make_unique<DriveStateListener>(this);
    DataReader* reader = getOrCreateReader(topic_ptr, listener.get());
    if (!reader) return false;
    listeners_[topic] = std::move(listener);
    std::cout << "✅ 订阅DriveState主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeFaultReport(const std::string& topic) {
    TypeSupport type_support(new FaultReportPubSubType());
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    auto listener = std::make_unique<FaultReportListener>(this);
    DataReader* reader = getOrCreateReader(topic_ptr, listener.get());
    if (!reader) return false;
    listeners_[topic] = std::move(listener);
    std::cout << "✅ 订阅FaultReport主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeAllTopics() {
    bool success = true;
    
    success &= subscribeHandshakeRequest("/handshake/request");
    success &= subscribeHandshakeResponse("/handshake/response");
    success &= subscribeVehicleStatus("/vehicle/vehicle_status");
    success &= subscribeRemoteControl("/vehicle/control_cmd");
    success &= subscribeDriveState("/vehicle/drive_state");
    success &= subscribeFaultReport("/vehicle/fault_report");

    if (success) {
        std::cout << "✅ 所有主题订阅成功" << std::endl;
    } else {
        std::cerr << "❌ 部分主题订阅失败" << std::endl;
    }
    
    return success;
}

// 内部辅助方法

Topic* FastDDSSubscriber::getOrCreateTopic(const std::string& topic_name, const std::string& type_name, TypeSupport& type_support) {
    // 检查是否已存在
    auto it = topics_.find(topic_name);
    if (it != topics_.end()) {
        return it->second;
    }
    
    // 注册类型
    if (participant_->register_type(type_support) != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
        std::cerr << "❌ 注册类型失败: " << type_name << std::endl;
        return nullptr;
    }
    
    // 创建主题
    Topic* topic = participant_->create_topic(topic_name, type_name, TOPIC_QOS_DEFAULT);
    if (topic == nullptr) {
        std::cerr << "❌ 创建主题失败: " << topic_name << std::endl;
        return nullptr;
    }
    
    // 保存引用
    topics_[topic_name] = topic;
    type_supports_[topic_name] = type_support;
    
    return topic;
}

DataReader* FastDDSSubscriber::getOrCreateReader(Topic* topic, DataReaderListener* listener) {
    const std::string& topic_name = topic->get_name();
    
    // 检查是否已存在
    auto it = readers_.find(topic_name);
    if (it != readers_.end()) {
        return it->second;
    }
    
    // 配置QoS
    DataReaderQos reader_qos;
    reader_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
    // 禁用数据共享以避免共享内存问题
    reader_qos.data_sharing().off();
    
    // 设置History和Resource Limits以支持更大的消息
    reader_qos.history().kind = KEEP_LAST_HISTORY_QOS;
    reader_qos.history().depth = 10;
    reader_qos.resource_limits().max_samples = 100;
    reader_qos.resource_limits().max_instances = 10;
    reader_qos.resource_limits().max_samples_per_instance = 10;
    
    // 设置端点属性以支持更大的payload（200字节，足够容纳136字节的消息）
    reader_qos.endpoint().history_memory_policy = eprosima::fastrtps::rtps::PREALLOCATED_WITH_REALLOC_MEMORY_MODE;
    
    // 创建数据读取器
    DataReader* reader = subscriber_->create_datareader(topic, reader_qos, listener);
    if (reader == nullptr) {
        std::cerr << "❌ 创建数据读取器失败: " << topic_name << std::endl;
        return nullptr;
    }
    
    // 保存引用
    readers_[topic_name] = reader;
    
    return reader;
}


