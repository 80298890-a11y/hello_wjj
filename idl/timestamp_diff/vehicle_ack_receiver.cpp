#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <iomanip>

// 包含生成的头文件
#include "VehicleAckPubSubTypes.hpp"

using namespace eprosima::fastdds::dds;

class DataReceiver
{
private:
    DomainParticipant* participant_;
    Subscriber* subscriber_;
    DataReader* reader_;
    Topic* topic_;
    TypeSupport type_;
    int message_count_;

    class SubListener : public DataReaderListener
    {
    public:
        SubListener(DataReceiver* receiver) : receiver_(receiver) {}

        void on_data_available(DataReader* reader) override
        {
            VehicleAck data;
            SampleInfo info;

            if (reader->take_next_sample(&data, &info) == RETCODE_OK)
            {
                if (info.valid_data)
                {
                    // 获取接收时间戳 a2
                    auto now = std::chrono::high_resolution_clock::now();
                    auto a2_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count();
                    
                    // 获取a1时间戳（从ack_timestamp_ns字段）
                    int64_t a1_timestamp = data.ack_timestamp_ns() / 1000000; // 转换为毫秒
                    
                    // 计算a2-a1的差值（毫秒）
                    int64_t a2_minus_a1_ms = a2_timestamp - a1_timestamp;
                    
                    // 打印时间戳信息
                    std::cout << "a1_timestamp: " << a1_timestamp << " ms" << std::endl;
                    std::cout << "a2_timestamp: " << a2_timestamp << " ms" << std::endl;
                    std::cout << "a2_minus_a1_ms: " << a2_minus_a1_ms << " ms" << std::endl;
                    
                    receiver_->message_count_++;
                    
                    // 每10条消息显示详细JSON
                    if (receiver_->message_count_ % 10 == 0) {
                        receiver_->print_detailed_json(data, receiver_->message_count_, a1_timestamp, a2_timestamp, a2_minus_a1_ms);
                    }
                }
            }
        }

    private:
        DataReceiver* receiver_;
    };

    SubListener listener_;

public:
    DataReceiver() : participant_(nullptr), subscriber_(nullptr), reader_(nullptr),
                    topic_(nullptr), listener_(this), message_count_(0)
    {
        type_ = TypeSupport(new VehicleAckPubSubType());
    }

    ~DataReceiver()
    {
        if (reader_ != nullptr)
        {
            subscriber_->delete_datareader(reader_);
        }
        if (subscriber_ != nullptr)
        {
            participant_->delete_subscriber(subscriber_);
        }
        if (topic_ != nullptr)
        {
            participant_->delete_topic(topic_);
        }
        DomainParticipantFactory::get_instance()->delete_participant(participant_);
    }

    bool init()
    {
        // 创建参与者
        participant_ = DomainParticipantFactory::get_instance()->create_participant(0, PARTICIPANT_QOS_DEFAULT);

        if (participant_ == nullptr)
        {
            return false;
        }

        // 注册类型
        type_.register_type(participant_);

        // 创建 Topic
        topic_ = participant_->create_topic("VehicleAck", "VehicleAck", TOPIC_QOS_DEFAULT);

        if (topic_ == nullptr)
        {
            return false;
        }

        // 创建订阅者
        subscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT, nullptr);

        if (subscriber_ == nullptr)
        {
            return false;
        }

        // 创建 DataReader
        DataReaderQos rqos = DATAREADER_QOS_DEFAULT;
        rqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
        reader_ = subscriber_->create_datareader(topic_, rqos, &listener_);

        if (reader_ == nullptr)
        {
            return false;
        }

        return true;
    }

    void print_detailed_json(const VehicleAck& data, int count, int64_t a1_timestamp, int64_t a2_timestamp, int64_t a2_minus_a1_ms)
    {
        std::cout << "接收第 " << count << " 条消息:" << std::endl;
        std::cout << "a1_timestamp: " << a1_timestamp << " ms" << std::endl;
        std::cout << "a2_timestamp: " << a2_timestamp << " ms" << std::endl;
        std::cout << "a2_minus_a1_ms: " << a2_minus_a1_ms << " ms" << std::endl;
        std::cout << "{" << std::endl;
        std::cout << "  \"sequence_id\": " << data.sequence_id() << "," << std::endl;
        std::cout << "  \"timestamp\": " << data.timestamp() << "," << std::endl;
        std::cout << "  \"status\": \"" << data.status() << "\"," << std::endl;
        std::cout << "  \"ack_timestamp_ns\": " << data.ack_timestamp_ns() << "," << std::endl;
        std::cout << "  \"steering_angle_enable\": " << (data.steering_angle_enable() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"steering_angle\": " << std::fixed << std::setprecision(2) << data.steering_angle() << "," << std::endl;
        std::cout << "  \"target_acceleration_enable\": " << (data.target_acceleration_enable() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"target_acceleration\": " << std::fixed << std::setprecision(2) << data.target_acceleration() << "," << std::endl;
        std::cout << "  \"indicator_left_enable\": " << (data.indicator_left_enable() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"indicator_left\": " << (data.indicator_left() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"indicator_right_enable\": " << (data.indicator_right_enable() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"indicator_right\": " << (data.indicator_right() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"gear_position_enable\": " << (data.gear_position_enable() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"gear_position\": " << data.gear_position() << "," << std::endl;
        std::cout << "  \"ebrake_status_enable\": " << (data.ebrake_status_enable() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"ebrake_status\": " << data.ebrake_status() << "," << std::endl;
        std::cout << "  \"remote_drive_ready\": " << (data.remote_drive_ready() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"remote_drive_takeover_request\": " << (data.remote_drive_takeover_request() ? "true" : "false") << std::endl;
        std::cout << "}" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
    }

    void run()
    {
        std::cout << "Data Receiver started. Waiting for VehicleAck messages..." << std::endl;
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

int main()
{
    DataReceiver receiver;

    if (receiver.init())
    {
        receiver.run();
    }
    else
    {
        std::cout << "Failed to initialize Data Receiver" << std::endl;
        return -1;
    }

    return 0;
}   