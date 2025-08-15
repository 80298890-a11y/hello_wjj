#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <iomanip>

// 包含生成的头文件
#include "CloudToVehiclePubSubTypes.hpp"
#include "VehicleAckPubSubTypes.hpp"

using namespace eprosima::fastdds::dds;

class DataForwarder
{
private:
    DomainParticipant* participant_;
    Subscriber* subscriber_;
    DataReader* reader_;
    Publisher* publisher_;
    DataWriter* writer_;
    Topic* topic1_;
    Topic* topic2_;
    TypeSupport type1_;
    TypeSupport type2_;
    int message_count_;

    class SubListener : public DataReaderListener
    {
    public:
        SubListener(DataForwarder* forwarder) : forwarder_(forwarder) {}

        void on_data_available(DataReader* reader) override
        {
            CloudToVehicle data;
            SampleInfo info;

            if (reader->take_next_sample(&data, &info) == RETCODE_OK)
            {
                if (info.valid_data)
                {
                    // 获取接收时间戳 b1
                    auto now = std::chrono::high_resolution_clock::now();
                    auto b1_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count();
                    
                    // 打印b1时间戳
                    std::cout << "b1_timestamp: " << b1_timestamp << " ms" << std::endl;

                    // 转发数据到 Topic 2
                    forwarder_->forward_data(data);
                }
            }
        }

    private:
        DataForwarder* forwarder_;
    };

    SubListener listener_;

public:
    DataForwarder() : participant_(nullptr), subscriber_(nullptr), reader_(nullptr),
                     publisher_(nullptr), writer_(nullptr), topic1_(nullptr), topic2_(nullptr),
                     listener_(this), message_count_(0)
    {
        type1_ = TypeSupport(new CloudToVehiclePubSubType());
        type2_ = TypeSupport(new VehicleAckPubSubType());
    }

    ~DataForwarder()
    {
        if (writer_ != nullptr)
        {
            publisher_->delete_datawriter(writer_);
        }
        if (publisher_ != nullptr)
        {
            participant_->delete_publisher(publisher_);
        }
        if (reader_ != nullptr)
        {
            subscriber_->delete_datareader(reader_);
        }
        if (subscriber_ != nullptr)
        {
            participant_->delete_subscriber(subscriber_);
        }
        if (topic1_ != nullptr)
        {
            participant_->delete_topic(topic1_);
        }
        if (topic2_ != nullptr)
        {
            participant_->delete_topic(topic2_);
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
        type1_.register_type(participant_);
        type2_.register_type(participant_);

        // 创建 Topic 1 (CloudToVehicle)
        topic1_ = participant_->create_topic("CloudToVehicle", "CloudToVehicle", TOPIC_QOS_DEFAULT);

        if (topic1_ == nullptr)
        {
            return false;
        }

        // 创建 Topic 2 (VehicleAck)
        topic2_ = participant_->create_topic("VehicleAck", "VehicleAck", TOPIC_QOS_DEFAULT);

        if (topic2_ == nullptr)
        {
            return false;
        }

        // 创建订阅者
        subscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT, nullptr);

        if (subscriber_ == nullptr)
        {
            return false;
        }

        // 创建发布者
        publisher_ = participant_->create_publisher(PUBLISHER_QOS_DEFAULT, nullptr);

        if (publisher_ == nullptr)
        {
            return false;
        }

        // 创建 DataReader
        DataReaderQos rqos = DATAREADER_QOS_DEFAULT;
        reader_ = subscriber_->create_datareader(topic1_, rqos, &listener_);

        if (reader_ == nullptr)
        {
            return false;
        }

        // 创建 DataWriter
        DataWriterQos wqos = DATAWRITER_QOS_DEFAULT;
        wqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
        writer_ = publisher_->create_datawriter(topic2_, wqos);

        if (writer_ == nullptr)
        {
            return false;
        }

        return true;
    }

    void forward_data(const CloudToVehicle& data)
    {
        // 创建 VehicleAck 消息并复制所有字段
        VehicleAck ack;
        
        // 复制所有存在的字段
        ack.sequence_id(data.sequence_id());
        ack.timestamp(data.timestamp());
        ack.steering_angle_enable(data.steering_angle_enable());
        ack.steering_angle(data.steering_angle());
        ack.target_acceleration_enable(data.target_acceleration_enable());
        ack.target_acceleration(data.target_acceleration());
        ack.indicator_left_enable(data.indicator_left_enable());
        ack.indicator_left(data.indicator_left());
        ack.indicator_right_enable(data.indicator_right_enable());
        ack.indicator_right(data.indicator_right());
        ack.gear_position_enable(data.gear_position_enable());
        ack.gear_position(data.gear_position());
        ack.ebrake_status_enable(data.ebrake_status_enable());
        ack.ebrake_status(data.ebrake_status());
        ack.remote_drive_ready(data.remote_drive_ready());
        ack.remote_drive_takeover_request(data.remote_drive_takeover_request());
        
        // 设置 VehicleAck 特有的字段
        ack.status("FORWARDED");
        
        // 保存真实的a1时间戳（从原始消息中获取）
        ack.ack_timestamp_ns(data.timestamp() * 1000000); // 转换为纳秒

        // 获取真实的发送时间戳 b2（在发送之前抓取）
        int64_t b2_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();

        // 发送数据
        writer_->write(&ack);
        
        // 打印真实的b2时间戳
        std::cout << "b2_timestamp: " << b2_timestamp << " ms" << std::endl;
        
        message_count_++;
        
        // 每10条消息打印一次完整的JSON
        if (message_count_ % 10 == 0) {
            print_detailed_json(ack, message_count_);
        }
    }

    void print_detailed_json(const VehicleAck& ack, int count)
    {
        std::cout << "转发第 " << count << " 条消息:" << std::endl;
        std::cout << "{" << std::endl;
        std::cout << "  \"sequence_id\": " << ack.sequence_id() << "," << std::endl;
        std::cout << "  \"timestamp\": " << ack.timestamp() << "," << std::endl;
        std::cout << "  \"status\": \"" << ack.status() << "\"," << std::endl;
        std::cout << "  \"ack_timestamp_ns\": " << ack.ack_timestamp_ns() << "," << std::endl;
        std::cout << "  \"steering_angle_enable\": " << (ack.steering_angle_enable() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"steering_angle\": " << std::fixed << std::setprecision(2) << ack.steering_angle() << "," << std::endl;
        std::cout << "  \"target_acceleration_enable\": " << (ack.target_acceleration_enable() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"target_acceleration\": " << std::fixed << std::setprecision(2) << ack.target_acceleration() << "," << std::endl;
        std::cout << "  \"indicator_left_enable\": " << (ack.indicator_left_enable() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"indicator_left\": " << (ack.indicator_left() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"indicator_right_enable\": " << (ack.indicator_right_enable() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"indicator_right\": " << (ack.indicator_right() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"gear_position_enable\": " << (ack.gear_position_enable() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"gear_position\": " << ack.gear_position() << "," << std::endl;
        std::cout << "  \"ebrake_status_enable\": " << (ack.ebrake_status_enable() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"ebrake_status\": " << ack.ebrake_status() << "," << std::endl;
        std::cout << "  \"remote_drive_ready\": " << (ack.remote_drive_ready() ? "true" : "false") << "," << std::endl;
        std::cout << "  \"remote_drive_takeover_request\": " << (ack.remote_drive_takeover_request() ? "true" : "false") << std::endl;
        std::cout << "}" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
    }

    void run()
    {
        std::cout << "Data Forwarder started. Waiting for CloudToVehicle messages..." << std::endl;
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

int main()
{
    DataForwarder forwarder;

    if (forwarder.init())
    {
        forwarder.run();
    }
    else
    {
        std::cout << "Failed to initialize Data Forwarder" << std::endl;
        return -1;
    }

    return 0;
}