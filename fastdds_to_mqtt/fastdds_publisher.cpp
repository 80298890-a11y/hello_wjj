/**
 * FastDDS发布者
 * 功能：生成并发送CloudToVehicle消息
 */
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>

#include "CloudToVehicle.hpp"
#include "CloudToVehiclePubSubTypes.hpp"

#include <chrono>
#include <thread>
#include <limits>

using namespace eprosima::fastdds::dds;
using namespace std::chrono;

// 精确sleep函数 - 动态调整等待时间以保持精确的100Hz频率
void precise_sleep_us(int64_t microseconds) {
    auto start = std::chrono::high_resolution_clock::now();
    auto target = start + std::chrono::microseconds(microseconds);
    
    while (std::chrono::high_resolution_clock::now() < target) {
        // 使用yield让出CPU时间片，减少CPU占用
        std::this_thread::yield();
    }
}

class CloudToVehiclePublisher {
private:
    DomainParticipant* participant_;
    Publisher* publisher_;
    Topic* topic_;
    DataWriter* writer_;
    TypeSupport type_;
    CloudToVehicle data_;
    int32_t sequence_id_;
    int64_t total_send_time_us_;
    int64_t max_send_time_us_;
    int64_t min_send_time_us_;

public:
    CloudToVehiclePublisher() : participant_(nullptr), publisher_(nullptr), 
                               topic_(nullptr), writer_(nullptr),
                               type_(new CloudToVehiclePubSubType()),
                               sequence_id_(0),
                               total_send_time_us_(0),
                               max_send_time_us_(0),
                               min_send_time_us_(std::numeric_limits<int64_t>::max())
    {
        init();
    }

    ~CloudToVehiclePublisher() {
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
        pqos.name("CloudToVehicle_Publisher");
        participant_ = DomainParticipantFactory::get_instance()->create_participant(0, pqos);
        if (participant_ == nullptr) {
            return false;
        }

        // 注册类型
        type_.register_type(participant_);

        // 创建主题
        topic_ = participant_->create_topic(
            "CloudToVehicle",
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
        writer_ = publisher_->create_datawriter(
            topic_,
            wqos);
        if (writer_ == nullptr) {
            return false;
        }

        return true;
    }

    bool publish() {
        // 记录发送开始时间
        auto function_start_time = high_resolution_clock::now();

        // 更新数据
        data_.sequence_id(sequence_id_++);
        data_.timestamp(duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count());
        
        // 设置所有字段
        data_.ebrake_status(0);
        data_.ebrake_status_enable(false);
        data_.gear_position(0);
        data_.gear_position_enable(false);
        data_.indicator_left(false);
        data_.indicator_left_enable(false);
        data_.indicator_right(false);
        data_.indicator_right_enable(false);
        data_.remote_drive_ready(false);
        data_.remote_drive_takeover_request(false);
        data_.steering_angle(0.0f);
        data_.steering_angle_enable(true);
        data_.target_acceleration(0.0f);
        data_.target_acceleration_enable(true);

        // 发送数据
        writer_->write(&data_);

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
        CloudToVehiclePublisher publisher;
        publisher.run();
    } catch (const std::exception& e) {
        return 1;
    }
    return 0;
}