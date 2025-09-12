/**
 * HandShake发布者
 * 功能：生成并发送HandShake消息（HandshakeRequest和HandshakeResponse）
 * 发送频率：100Hz（每10ms发送一次）
 */
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>

#include "Handshake.hpp"
#include "HandshakePubSubTypes.hpp"

#include <chrono>
#include <thread>
#include <limits>
#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <fstream>

using namespace eprosima::fastdds::dds;
using namespace std::chrono;
using namespace remote_msgs;

// 精确sleep函数 - 动态调整等待时间以保持精确的100Hz频率
void precise_sleep_us(int64_t microseconds) {
    auto start = std::chrono::high_resolution_clock::now();
    auto target = start + std::chrono::microseconds(microseconds);
    
    while (std::chrono::high_resolution_clock::now() < target) {
        // 使用yield让出CPU时间片，减少CPU占用
        std::this_thread::yield();
    }
}

// 业务值定义常量 - 直接使用原始值
namespace BusinessValues {
    // noa_active_request/response
    const long DEACTIVE = -1;
    const long NORMAL = 0;
    const long ACTIVE = 1;
    
    // RemoteOverrideResponse  
    const long FAIL = -1;
    const long DEACTIVE_OVERRIDE = 0;
    const long READY = 1;
    const long ACTIVE_OVERRIDE = 2;
}

// 简化的异步日志系统 - 只记录发送数据
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

class HandShakePublisher {
private:
    DomainParticipant* participant_;
    Publisher* publisher_;
    Topic* request_topic_;
    Topic* response_topic_;
    DataWriter* request_writer_;
    DataWriter* response_writer_;
    TypeSupport request_type_;
    TypeSupport response_type_;
    HandshakeRequest request_data_;
    HandshakeResponse response_data_;
    int32_t sequence_id_;
    int64_t total_send_time_us_;
    int64_t max_send_time_us_;
    int64_t min_send_time_us_;
    std::unique_ptr<AsyncLogger> logger_;

public:
    HandShakePublisher() : participant_(nullptr), publisher_(nullptr),
                          request_topic_(nullptr), response_topic_(nullptr),
                          request_writer_(nullptr), response_writer_(nullptr),
                          request_type_(new HandshakeRequestPubSubType()),
                          response_type_(new HandshakeResponsePubSubType()),
                          sequence_id_(0),
                          total_send_time_us_(0),
                          max_send_time_us_(0),
                          min_send_time_us_(std::numeric_limits<int64_t>::max())
    {
        // 初始化异步日志
        logger_ = std::make_unique<AsyncLogger>();
        init();
    }

    ~HandShakePublisher() {
        if (request_writer_ != nullptr) {
            publisher_->delete_datawriter(request_writer_);
        }
        if (response_writer_ != nullptr) {
            publisher_->delete_datawriter(response_writer_);
        }
        if (publisher_ != nullptr) {
            participant_->delete_publisher(publisher_);
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
        pqos.name("HandShake_Publisher");
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

        // 创建发布者
        publisher_ = participant_->create_publisher(PUBLISHER_QOS_DEFAULT);
        if (publisher_ == nullptr) {
            return false;
        }

        // 创建数据写入器
        DataWriterQos wqos;
        wqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
        request_writer_ = publisher_->create_datawriter(request_topic_, wqos);
        response_writer_ = publisher_->create_datawriter(response_topic_, wqos);

        if (request_writer_ == nullptr || response_writer_ == nullptr) {
            return false;
        }

        return true;
    }

    bool publish() {
        // 记录发送开始时间
        auto function_start_time = high_resolution_clock::now();

        // 获取发送时间戳
        uint64_t send_timestamp = duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();

        // 更新握手请求数据 - 固定值：枚举值为0，bool值为true
        request_data_.noa_active(0);        // 固定值：0
        request_data_.override_status(0);   // 固定值：0
        request_data_.override_ready(0);    // 固定值：0
        request_data_.timestamp(send_timestamp);

        // 更新握手响应数据 - 固定值：枚举值为0，bool值为true
        response_data_.noa_active(0);           // 固定值：0
        response_data_.override_response(0);    // 固定值：0
        response_data_.current_control_source(true);  // 固定值：true
        response_data_.timestamp(send_timestamp);

        // 发送数据
        request_writer_->write(&request_data_);
        response_writer_->write(&response_data_);

        // 异步记录发送日志（透传数据，不影响发送时效）
        logger_->log("noa_active=" + std::to_string(request_data_.noa_active()) +
                    ", override_status=" + std::to_string(request_data_.override_status()) +
                    ", override_ready=" + std::to_string(request_data_.override_ready()) +
                    ", timestamp=" + std::to_string(send_timestamp));
        logger_->log("noa_active=" + std::to_string(response_data_.noa_active()) +
                    ", override_response=" + std::to_string(response_data_.override_response()) +
                    ", current_control_source=" + (response_data_.current_control_source() ? "true" : "false") +
                    ", timestamp=" + std::to_string(send_timestamp));

        // 记录发送结束时间并计算耗时
        auto function_end_time = high_resolution_clock::now();
        auto send_time_us = duration_cast<microseconds>(function_end_time - function_start_time).count();

        // 更新统计信息（仅内部记录，不打印）
        total_send_time_us_ += send_time_us;
        max_send_time_us_ = std::max(max_send_time_us_, send_time_us);
        min_send_time_us_ = std::min(min_send_time_us_, send_time_us);

        return true;
    }

    void run() {
        const int64_t TARGET_INTERVAL_US = 10000;  // 10ms = 10000微秒
        
        std::cout << "HandShake Publisher started at 100Hz" << std::endl;
        
        while (true) {
            // 记录循环开始时间
            auto loop_start_time = high_resolution_clock::now();
            
            if (publish()) {
                // 计算实际发送耗时
                auto current_time = high_resolution_clock::now();
                auto elapsed_us = duration_cast<microseconds>(current_time - loop_start_time).count();
                
                // 计算需要等待的时间（动态调整以保持精确的100Hz频率）
                int64_t sleep_time_us = TARGET_INTERVAL_US - elapsed_us;
                
                // 确保sleep时间不为负数
                if (sleep_time_us > 0) {
                    precise_sleep_us(sleep_time_us);
                }
            }
        }
    }
};

int main() {
    try {
        HandShakePublisher publisher;
        publisher.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
