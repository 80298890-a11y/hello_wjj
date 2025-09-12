/**
 * VehicleStatus发布者
 * 功能：生成并发送VehicleStatus消息
 * 发送频率：100Hz（每10ms发送一次）
 */
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>

#include "VehicleStatus.hpp"
#include "VehicleStatusPubSubTypes.hpp"

#include <chrono>
#include <thread>
#include <limits>
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

// 精确sleep函数 - 动态调整等待时间以保持精确的100Hz频率
void precise_sleep_us(int64_t microseconds) {
    auto start = std::chrono::high_resolution_clock::now();
    auto target = start + std::chrono::microseconds(microseconds);
    
    while (std::chrono::high_resolution_clock::now() < target) {
        // 使用yield让出CPU时间片，减少CPU占用
        std::this_thread::yield();
    }
}

class VehicleStatusPublisher {
private:
    DomainParticipant* participant_;
    Publisher* publisher_;
    Topic* topic_;
    DataWriter* writer_;
    TypeSupport type_;
    VehicleStatus data_;
    int32_t sequence_id_;
    int64_t total_send_time_us_;
    int64_t max_send_time_us_;
    int64_t min_send_time_us_;
    std::unique_ptr<AsyncLogger> logger_;
    
    // 固定值模拟数据（不再使用随机数）

public:
    VehicleStatusPublisher() : participant_(nullptr), publisher_(nullptr),
                              topic_(nullptr), writer_(nullptr),
                              type_(new VehicleStatusPubSubType()),
                              sequence_id_(0),
                              total_send_time_us_(0),
                              max_send_time_us_(0),
                              min_send_time_us_(std::numeric_limits<int64_t>::max())
    {
        // 初始化异步日志
        logger_ = std::make_unique<AsyncLogger>();
        init();
    }

    ~VehicleStatusPublisher() {
        if (writer_ != nullptr) {
            publisher_->delete_datawriter(writer_);
        }
        if (publisher_ != nullptr) {
            participant_->delete_publisher(publisher_);
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
        pqos.name("VehicleStatus_Publisher");
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

        // 创建发布者
        publisher_ = participant_->create_publisher(PUBLISHER_QOS_DEFAULT);
        if (publisher_ == nullptr) {
            return false;
        }

        // 创建数据写入器
        DataWriterQos wqos;
        wqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
        writer_ = publisher_->create_datawriter(topic_, wqos);
        if (writer_ == nullptr) {
            return false;
        }

        return true;
    }

    bool publish() {
        // 记录发送开始时间
        auto function_start_time = high_resolution_clock::now();

        // 固定车辆状态数据：枚举值为0，bool值为true
        data_.vehicle_id(12345);
        data_.current_control_mode(control_mode::Auto);  // 固定值：第一个枚举值
        data_.position_longitude(116.397428f);  // 固定经度
        data_.position_latitude(39.90923f);     // 固定纬度
        data_.position_altitude(50.0f);
        data_.speed(30.0f);             // 固定速度：30 km/h
        data_.yawrate(0.1f);
        data_.gear_position(2.0f);      // D档
        data_.acceleration(0.5f);
        data_.heading(0.0f);            // 固定朝向：0度
        data_.steering_angle(0.0f);     // 固定转向角：0度
        data_.wheel_angle(0.0f);        // 固定轮角：0度
        data_.ebrake_status(0);         // 固定值：0
        data_.indicator_left(0);        // 固定值：0
        data_.indicator_right(0);       // 固定值：0
        data_.power_mode(PowerModeType::POWERMODE_OFF);  // 固定值：第一个枚举值
        data_.brake_light_status(true); // 固定值：true
        data_.high_beam_status(true);   // 固定值：true
        data_.low_beam_status(true);    // 固定值：true
        data_.emergency_light_status(true); // 固定值：true
        
        // 获取发送时间戳
        uint64_t send_timestamp = duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
        data_.timestamp(send_timestamp);

        // 发送数据
        writer_->write(&data_);

        // 异步记录发送日志（透传数据，不影响发送时效）
        logger_->log(" vehicle_id=" + std::to_string(data_.vehicle_id()) +
                    ", control_mode=" + std::to_string(static_cast<int>(data_.current_control_mode())) +
                    ", position_longitude=" + std::to_string(data_.position_longitude()) +
                    ", position_latitude=" + std::to_string(data_.position_latitude()) +
                    ", speed=" + std::to_string(data_.speed()) +
                    ", power_mode=" + std::to_string(static_cast<int>(data_.power_mode())) +
                    ", brake_light_status=" + (data_.brake_light_status() ? "true" : "false") +
                    ", timestamp=" + std::to_string(send_timestamp));
        sequence_id_++;

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
        
        std::cout << "VehicleStatus Publisher started at 100Hz" << std::endl;
        
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
        VehicleStatusPublisher publisher;
        publisher.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
