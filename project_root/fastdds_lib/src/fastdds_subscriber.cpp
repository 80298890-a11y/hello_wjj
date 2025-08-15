#include "fastdds_subscriber.hpp"

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

// =========================== 监听器类实现 ===========================

class FastDDSSubscriber::HandshakeRequestListener : public DataReaderListener {
private:
    FastDDSSubscriber* subscriber_;

public:
    HandshakeRequestListener(FastDDSSubscriber* subscriber) : subscriber_(subscriber) {}

    void on_data_available(DataReader* reader) override {
        SampleInfo info;
        HandshakeRequest request;
        
        if (reader->take_next_sample(&request, &info) == RETCODE_OK) {
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
        
        if (reader->take_next_sample(&response, &info) == RETCODE_OK) {
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
        
        if (reader->take_next_sample(&status, &info) == RETCODE_OK) {
            if (info.valid_data && subscriber_->vehicle_status_callback_) {
                subscriber_->vehicle_status_callback_(
                    status.vehicle_id(),
                    static_cast<int>(status.control_mode()),
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
        
        if (reader->take_next_sample(&control, &info) == RETCODE_OK) {
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

// =========================== FastDDSSubscriber实现 ===========================

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
    // 创建参与者
    DomainParticipantQos participant_qos;
    participant_qos.name("FastDDSSubscriber_Participant");
    
    participant_ = DomainParticipantFactory::get_instance()->create_participant(
        domain_id_, participant_qos);
    
    if (participant_ == nullptr) {
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

// =========================== 回调函数设置 ===========================

void FastDDSSubscriber::setHandshakeRequestCallback(HandshakeRequestCallback callback) {
    handshake_request_callback_ = callback;
}

void FastDDSSubscriber::setHandshakeResponseCallback(HandshakeResponseCallback callback) {
    handshake_response_callback_ = callback;
}

void FastDDSSubscriber::setVehicleStatusCallback(VehicleStatusCallback callback) {
    vehicle_status_callback_ = callback;
}

void FastDDSSubscriber::setRemoteControlCallback(RemoteControlCallback callback) {
    remote_control_callback_ = callback;
}

// =========================== 订阅功能 ===========================

bool FastDDSSubscriber::subscribeHandshakeRequest(const std::string& topic) {
    // 创建类型支持
    TypeSupport type_support(new HandshakeRequestPubSubType());
    
    // 获取或创建主题  
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    
    // 创建监听器
    auto listener = std::make_unique<HandshakeRequestListener>(this);
    DataReaderListener* listener_ptr = listener.get();
    
    // 获取或创建数据读取器
    DataReader* reader = getOrCreateReader(topic_ptr, listener_ptr);
    if (!reader) return false;
    
    // 保存监听器
    listeners_[topic] = std::move(listener);
    
    std::cout << "✅ 订阅HandshakeRequest主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeHandshakeResponse(const std::string& topic) {
    // 创建类型支持
    TypeSupport type_support(new HandshakeResponsePubSubType());
    
    // 获取或创建主题  
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    
    // 创建监听器
    auto listener = std::make_unique<HandshakeResponseListener>(this);
    DataReaderListener* listener_ptr = listener.get();
    
    // 获取或创建数据读取器
    DataReader* reader = getOrCreateReader(topic_ptr, listener_ptr);
    if (!reader) return false;
    
    // 保存监听器
    listeners_[topic] = std::move(listener);
    
    std::cout << "✅ 订阅HandshakeResponse主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeVehicleStatus(const std::string& topic) {
    // 创建类型支持
    TypeSupport type_support(new VehicleStatusPubSubType());
    
    // 获取或创建主题
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    
    // 创建监听器
    auto listener = std::make_unique<VehicleStatusListener>(this);
    DataReaderListener* listener_ptr = listener.get();
    
    // 获取或创建数据读取器
    DataReader* reader = getOrCreateReader(topic_ptr, listener_ptr);
    if (!reader) return false;
    
    // 保存监听器
    listeners_[topic] = std::move(listener);
    
    std::cout << "✅ 订阅VehicleStatus主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeRemoteControl(const std::string& topic) {
    // 创建类型支持
    TypeSupport type_support(new ControlCmdPubSubType());
    
    // 获取或创建主题
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    
    // 创建监听器
    auto listener = std::make_unique<RemoteControlListener>(this);
    DataReaderListener* listener_ptr = listener.get();
    
    // 获取或创建数据读取器
    DataReader* reader = getOrCreateReader(topic_ptr, listener_ptr);
    if (!reader) return false;
    
    // 保存监听器
    listeners_[topic] = std::move(listener);
    
    std::cout << "✅ 订阅RemoteControl主题: " << topic << std::endl;
    return true;
}

bool FastDDSSubscriber::subscribeAllTopics() {
    bool success = true;
    
    success &= subscribeHandshakeRequest("/handshake/request");
    success &= subscribeHandshakeResponse("/handshake/response");
    success &= subscribeVehicleStatus("/vehicle/vehicle_status");
    success &= subscribeRemoteControl("/vehicle/control_cmd");
    
    if (success) {
        std::cout << "✅ 所有主题订阅成功" << std::endl;
    } else {
        std::cerr << "❌ 部分主题订阅失败" << std::endl;
    }
    
    return success;
}

// =========================== 内部辅助方法 ===========================

Topic* FastDDSSubscriber::getOrCreateTopic(const std::string& topic_name, const std::string& type_name, TypeSupport& type_support) {
    // 检查是否已存在
    auto it = topics_.find(topic_name);
    if (it != topics_.end()) {
        return it->second;
    }
    
    // 注册类型
    if (participant_->register_type(type_support) != RETCODE_OK) {
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
