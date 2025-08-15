/**
 * HandShake订阅者
 * 功能：接收HandShake消息（HandshakeRequest和HandshakeResponse）
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

#include "Handshake.hpp"
#include "HandshakePubSubTypes.hpp"
#include "mqtt_mosquitto.hpp"

#include <chrono>
#include <thread>
#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <fstream>

using namespace eprosima::fastdds::dds;
using namespace std::chrono;
using namespace remote_msgs;

// 简化的异步日志系统 - 只记录接收数据
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

class HandShakeSubscriber {
private:
    DomainParticipant* participant_;
    Subscriber* subscriber_;
    Topic* request_topic_;
    Topic* response_topic_;
    DataReader* request_reader_;
    DataReader* response_reader_;
    TypeSupport request_type_;
    TypeSupport response_type_;
    std::unique_ptr<AsyncLogger> logger_;
    std::unique_ptr<MQTTMosquitto> mqtt_bridge_;

    // HandShake请求监听器
    class RequestListener : public DataReaderListener {
    private:
        HandShakeSubscriber* subscriber_;
    public:
        RequestListener(HandShakeSubscriber* sub) : subscriber_(sub) {}
        
        void on_data_available(DataReader* reader) override {
            HandshakeRequest request;
            SampleInfo info;
            
            while (reader->take_next_sample(&request, &info) == RETCODE_OK) {
                if (info.valid_data) {
                    // 记录接收时间
                    uint64_t receive_timestamp = duration_cast<milliseconds>(
                        system_clock::now().time_since_epoch()).count();
                    
                    // 创建MQTT消息内容（模拟Json::FastWriter格式）
                    std::string mqtt_payload = "{"
                                              "\"noa_active\":" + std::to_string(request.noa_active()) + ","
                                              "\"override_status\":" + std::to_string(request.override_status()) + ","
                                              "\"override_ready\":" + std::to_string(request.override_ready()) + ","
                                              "\"timestamp\":" + std::to_string(request.timestamp()) + ","
                                              "\"receive_timestamp\":" + std::to_string(receive_timestamp) + ""
                                              "}\n";  // 添加换行符，模拟Json::FastWriter
                    
                    // 发送到MQTT
                    subscriber_->mqtt_bridge_->publish_async("/remote/override_request", mqtt_payload);
                    
                    // 异步记录接收日志（透传数据）
                    subscriber_->logger_->log("noa_active=" + std::to_string(request.noa_active()) +
                                            ", override_status=" + std::to_string(request.override_status()) +
                                            ", override_ready=" + std::to_string(request.override_ready()) +
                                            ", send_timestamp=" + std::to_string(request.timestamp()) +
                                            ", receive_timestamp=" + std::to_string(receive_timestamp));
                }
            }
        }

        void on_subscription_matched(DataReader*, const SubscriptionMatchedStatus& info) override {
            if (info.current_count_change == 1) {
                std::cout << "HandshakeRequest Subscriber matched." << std::endl;
            } else if (info.current_count_change == -1) {
                std::cout << "HandshakeRequest Subscriber unmatched." << std::endl;
            }
        }
    } request_listener_;

    // HandShake响应监听器  
    class ResponseListener : public DataReaderListener {
    private:
        HandShakeSubscriber* subscriber_;
    public:
        ResponseListener(HandShakeSubscriber* sub) : subscriber_(sub) {}
        
        void on_data_available(DataReader* reader) override {
            HandshakeResponse response;
            SampleInfo info;
            
            while (reader->take_next_sample(&response, &info) == RETCODE_OK) {
                if (info.valid_data) {
                    // 记录接收时间
                    uint64_t receive_timestamp = duration_cast<milliseconds>(
                        system_clock::now().time_since_epoch()).count();
                    
                    // 创建MQTT消息内容（模拟Json::FastWriter格式）
                    std::string mqtt_payload = "{"
                                              "\"noa_active\":" + std::to_string(response.noa_active()) + ","
                                              "\"override_response\":" + std::to_string(response.override_response()) + ","
                                              "\"current_control_source\":" + (response.current_control_source() ? "true" : "false") + ","
                                              "\"timestamp\":" + std::to_string(response.timestamp()) + ","
                                              "\"receive_timestamp\":" + std::to_string(receive_timestamp) + ""
                                              "}\n";  // 添加换行符，模拟Json::FastWriter
                    
                    // 发送到MQTT
                    subscriber_->mqtt_bridge_->publish_async("/remote/override_response", mqtt_payload);
                    
                    // 异步记录接收日志（透传数据）
                    subscriber_->logger_->log("noa_active=" + std::to_string(response.noa_active()) +
                                            ", override_response=" + std::to_string(response.override_response()) +
                                            ", current_control_source=" + (response.current_control_source() ? "true" : "false") +
                                            ", send_timestamp=" + std::to_string(response.timestamp()) +
                                            ", receive_timestamp=" + std::to_string(receive_timestamp) +
                                            "");
                }
            }
        }

        void on_subscription_matched(DataReader*, const SubscriptionMatchedStatus& info) override {
            if (info.current_count_change == 1) {
                std::cout << "HandshakeResponse Subscriber matched." << std::endl;
            } else if (info.current_count_change == -1) {
                std::cout << "HandshakeResponse Subscriber unmatched." << std::endl;
            }
        }
    } response_listener_;

public:
    HandShakeSubscriber() 
        : participant_(nullptr), subscriber_(nullptr), 
          request_topic_(nullptr), response_topic_(nullptr),
          request_reader_(nullptr), response_reader_(nullptr),
          request_type_(new HandshakeRequestPubSubType()),
          response_type_(new HandshakeResponsePubSubType()),
          request_listener_(this), response_listener_(this)
    {
        // 初始化异步日志
        logger_ = std::make_unique<AsyncLogger>();
        
        // 初始化MQTT桥接（自动选择最佳实现）
        mqtt_bridge_ = std::make_unique<MQTTMosquitto>("hellorobotaxi.cn", 11883, "mqttx_ae1f7a37");
        if (!mqtt_bridge_->connect()) {
            std::cerr << "警告: MQTT连接失败，将只记录本地日志" << std::endl;
        }
        
        if (!init()) {
            throw std::runtime_error("HandShake Subscriber 初始化失败");
        }
    }

    ~HandShakeSubscriber() {
        // 清理FastDDS资源
        if (request_reader_ != nullptr) {
            subscriber_->delete_datareader(request_reader_);
        }
        if (response_reader_ != nullptr) {
            subscriber_->delete_datareader(response_reader_);
        }
        if (subscriber_ != nullptr) {
            participant_->delete_subscriber(subscriber_);
        }
        if (request_topic_ != nullptr) {
            participant_->delete_topic(request_topic_);
        }
        if (response_topic_ != nullptr) {
            participant_->delete_topic(response_topic_);
        }
        if (participant_ != nullptr) {
            DomainParticipantFactory::get_instance()->delete_participant(participant_);
        }
    }

    bool init() {
        // 创建参与者
        DomainParticipantQos pqos;
        pqos.name("HandShake_Subscriber");
        participant_ = DomainParticipantFactory::get_instance()->create_participant(0, pqos);
        if (participant_ == nullptr) {
            return false;
        }

        // 注册类型
        request_type_.register_type(participant_);
        response_type_.register_type(participant_);

        // 创建主题
        request_topic_ = participant_->create_topic(
            "/remote/override_request",
            request_type_.get_type_name(),
            TOPIC_QOS_DEFAULT);
        response_topic_ = participant_->create_topic(
            "/remote/override_response",
            response_type_.get_type_name(),
            TOPIC_QOS_DEFAULT);

        if (request_topic_ == nullptr || response_topic_ == nullptr) {
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
        request_reader_ = subscriber_->create_datareader(
            request_topic_,
            rqos,
            &request_listener_);
        response_reader_ = subscriber_->create_datareader(
            response_topic_,
            rqos,
            &response_listener_);

        if (request_reader_ == nullptr || response_reader_ == nullptr) {
            return false;
        }

        return true;
    }

    void run() {
        std::cout << "HandShake Subscriber started - waiting for messages..." << std::endl;
        
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
};

int main() {
    try {
        HandShakeSubscriber subscriber;
        subscriber.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
