/**
 * VehicleStatus订阅者
 * 功能：接收VehicleStatus消息
 */
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/core/status/StatusMask.hpp>
#include <fastdds/dds/core/policy/QosPolicies.hpp>
#include <fastdds/dds/core/status/BaseStatus.hpp>

#include "VehicleStatus.hpp"
#include "VehicleStatusPubSubTypes.hpp"
#include "mqtt_mosquitto.hpp"

#include <chrono>
#include <thread>
#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace eprosima::fastdds::dds;
using namespace std::chrono;
using namespace remote_msgs;

// 简化的异步日志系统
class AsyncLogger {
private:
    std::queue<std::string> log_queue_;
    std::mutex log_mutex_;
    std::condition_variable log_cv_;
    std::thread log_thread_;
    bool running_;

public:
    AsyncLogger() : running_(true) {
        // 启动异步输出线程
        log_thread_ = std::thread(&AsyncLogger::log_worker, this);
    }
    
    ~AsyncLogger() {
        // 停止日志线程
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            running_ = false;
        }
        log_cv_.notify_one();
        
        if (log_thread_.joinable()) {
            log_thread_.join();
        }
    }
    
    void log(const std::string& message) {
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            if (running_) {
                log_queue_.push(message);
            }
        }
        log_cv_.notify_one();
    }
    
private:
    void log_worker() {
        while (running_ || !log_queue_.empty()) {
            std::unique_lock<std::mutex> lock(log_mutex_);
            
            // 等待新数据或停止信号
            log_cv_.wait_for(lock, std::chrono::milliseconds(50), [this] {
                return !log_queue_.empty() || !running_;
            });
            
            // 批量输出到控制台
            while (!log_queue_.empty()) {
                std::cout << log_queue_.front() << std::endl;
                log_queue_.pop();
            }
            lock.unlock();
        }
    }
};

class VehicleStatusSubscriber {
private:
    DomainParticipant* participant_;
    Subscriber* subscriber_;
    Topic* topic_;
    DataReader* reader_;
    TypeSupport type_;
    std::unique_ptr<AsyncLogger> logger_;
    std::unique_ptr<MQTTMosquitto> mqtt_bridge_;

    class SubListener : public DataReaderListener {
    private:
        VehicleStatusSubscriber* subscriber_;
    public:
        SubListener(VehicleStatusSubscriber* sub) : subscriber_(sub) {}
        
        void on_data_available(DataReader* reader) override {
            VehicleStatus vehicle_status;
            SampleInfo info;
            
            while (reader->take_next_sample(&vehicle_status, &info) == RETCODE_OK) {
                if (info.valid_data) {
                    // 记录接收时间
                    uint64_t receive_timestamp = duration_cast<milliseconds>(
                        system_clock::now().time_since_epoch()).count();
                    
                    // 创建MQTT消息内容（简化JSON格式，参考工作版本）
                    std::string mqtt_payload = "{"
                                              "\"vehicle_id\":" + std::to_string(vehicle_status.vehicle_id()) + ","
                                              "\"speed\":" + std::to_string(vehicle_status.speed()) + ","
                                              "\"control_mode\":" + std::to_string(static_cast<int>(vehicle_status.current_control_mode())) + ","
                                              "\"longitude\":" + std::to_string(vehicle_status.position_longitude()) + ","
                                              "\"latitude\":" + std::to_string(vehicle_status.position_latitude()) + ","
                                              "\"altitude\":" + std::to_string(vehicle_status.position_altitude()) + ","
                                              "\"heading\":" + std::to_string(vehicle_status.heading()) + ","
                                              "\"gear_position\":" + std::to_string(vehicle_status.gear_position()) + ","
                                              "\"power_mode\":" + std::to_string(static_cast<int>(vehicle_status.power_mode())) + ","
                                              "\"timestamp\":" + std::to_string(vehicle_status.timestamp()) + ","
                                              "\"receive_timestamp\":" + std::to_string(receive_timestamp) + ""
                                              "}\n";  // 添加换行符，模拟Json::FastWriter
                    
                    // 发送到MQTT
                    subscriber_->mqtt_bridge_->publish_async("/remote/vehicle_status", mqtt_payload);
                    
                    // 异步记录接收日志（透传数据）
                    subscriber_->logger_->log("vehicle_id=" + std::to_string(vehicle_status.vehicle_id()) +
                                            ", speed=" + std::to_string(vehicle_status.speed()) + "km/h" +
                                            ", control_mode=" + std::to_string(static_cast<int>(vehicle_status.current_control_mode())) +
                                            ", longitude=" + std::to_string(vehicle_status.position_longitude()) +
                                            ", latitude=" + std::to_string(vehicle_status.position_latitude()) +
                                            ", heading=" + std::to_string(vehicle_status.heading()) + "°" +
                                            ", gear=" + std::to_string(vehicle_status.gear_position()) +
                                            ", power_mode=" + std::to_string(static_cast<int>(vehicle_status.power_mode())) +
                                            ", send_timestamp=" + std::to_string(vehicle_status.timestamp()) +
                                            ", receive_timestamp=" + std::to_string(receive_timestamp));
                }
            }
        }

        void on_subscription_matched(DataReader*, const SubscriptionMatchedStatus& info) override {
            if (info.current_count_change == 1) {
                std::cout << "VehicleStatus Subscriber matched." << std::endl;
            } else if (info.current_count_change == -1) {
                std::cout << "VehicleStatus Subscriber unmatched." << std::endl;
            }
        }
    } listener_;

public:
    VehicleStatusSubscriber() 
        : participant_(nullptr), subscriber_(nullptr), topic_(nullptr), reader_(nullptr),
          type_(new VehicleStatusPubSubType()),
          listener_(this)
    {
        // 初始化异步日志
        logger_ = std::make_unique<AsyncLogger>();
        
        // 初始化MQTT桥接（使用libmosquitto）
        mqtt_bridge_ = std::make_unique<MQTTMosquitto>("hellorobotaxi.cn", 11883, "mqttx_ae1f7a37");
        if (!mqtt_bridge_->connect()) {
            std::cerr << "警告: MQTT连接失败，将只记录本地日志" << std::endl;
        }
        
        if (!init()) {
            throw std::runtime_error("VehicleStatus Subscriber 初始化失败");
        }
    }

    ~VehicleStatusSubscriber() {
        // 清理FastDDS资源
        if (reader_ != nullptr) {
            subscriber_->delete_datareader(reader_);
        }
        if (subscriber_ != nullptr) {
            participant_->delete_subscriber(subscriber_);
        }
        if (topic_ != nullptr) {
            participant_->delete_topic(topic_);
        }
        if (participant_ != nullptr) {
            DomainParticipantFactory::get_instance()->delete_participant(participant_);
        }
    }

    bool init() {
        // 创建参与者
        DomainParticipantQos pqos;
        pqos.name("VehicleStatus_Subscriber");
        participant_ = DomainParticipantFactory::get_instance()->create_participant(0, pqos);
        if (participant_ == nullptr) {
            return false;
        }

        // 注册类型
        type_.register_type(participant_);

        // 创建主题
        topic_ = participant_->create_topic(
            "/remote/vehicle_status",
            type_.get_type_name(),
            TOPIC_QOS_DEFAULT);
        if (topic_ == nullptr) {
            return false;
        }

        // 创建订阅者
        subscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
        if (subscriber_ == nullptr) {
            return false;
        }

        // 创建数据读取器
        DataReaderQos rqos;
        rqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
        reader_ = subscriber_->create_datareader(
            topic_,
            rqos,
            &listener_);
        if (reader_ == nullptr) {
            return false;
        }

        return true;
    }

    void run() {
        std::cout << "VehicleStatus Subscriber started - waiting for messages..." << std::endl;
        
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
};

int main() {
    try {
        VehicleStatusSubscriber subscriber;
        subscriber.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
