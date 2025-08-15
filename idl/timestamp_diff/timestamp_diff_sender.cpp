#include <iostream>
#include <chrono>
#include <map>
#include <thread>
#include <csignal>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>

#include "../cloud_to_vehicle_dds/CloudToVehicleCommandPubSubTypes.hpp"
#include "../cloud_to_vehicle_dds/CloudToVehicleCommandApplication.hpp"
#include "../cloud_to_vehicle_dds/vehicleAckPubSubTypes.hpp"
#include "../cloud_to_vehicle_dds/vehicleAck.hpp"

using namespace eprosima::fastdds::dds;

class LatencyTester : public DataReaderListener
{
private:
    DomainParticipant* participant_;
    Publisher* publisher_;
    Subscriber* subscriber_;
    Topic* command_topic_;
    Topic* ack_topic_;
    DataWriter* command_writer_;
    DataReader* ack_reader_;
    
    long long sequence_id_;
    std::map<long long, std::chrono::high_resolution_clock::time_point> send_times_;
    bool running_;

public:
    LatencyTester() : participant_(nullptr), publisher_(nullptr), subscriber_(nullptr),
                     command_topic_(nullptr), ack_topic_(nullptr), command_writer_(nullptr),
                     ack_reader_(nullptr), sequence_id_(0), running_(true) {}

    bool init()
    {
        DomainParticipantFactory* factory = DomainParticipantFactory::get_instance();
        participant_ = factory->create_participant(10, PARTICIPANT_QOS_DEFAULT);
        if (participant_ == nullptr) return false;

        TypeSupport command_type(new CloudToVehicleCommandPubSubType());
        TypeSupport ack_type(new VehicleAckPubSubType());
        participant_->register_type(command_type);
        participant_->register_type(ack_type);

        command_topic_ = participant_->create_topic("CloudToVehicleTopic", "CloudToVehicleCommand", TOPIC_QOS_DEFAULT);
        ack_topic_ = participant_->create_topic("VehicleAckTopic", "VehicleAck", TOPIC_QOS_DEFAULT);
        if (command_topic_ == nullptr || ack_topic_ == nullptr) return false;

        publisher_ = participant_->create_publisher(PUBLISHER_QOS_DEFAULT);
        subscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
        if (publisher_ == nullptr || subscriber_ == nullptr) return false;

        command_writer_ = publisher_->create_datawriter(command_topic_, DATAWRITER_QOS_DEFAULT);
        if (command_writer_ == nullptr) return false;

        ack_reader_ = subscriber_->create_datareader(ack_topic_, DATAREADER_QOS_DEFAULT, this);
        if (ack_reader_ == nullptr) return false;

        return true;
    }

    void send_command()
    {
        sequence_id_++;
        auto send_time = std::chrono::high_resolution_clock::now();
        auto timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(send_time.time_since_epoch()).count();

        CloudToVehicleCommand command;
        command.sequence_id(sequence_id_);
        command.steering_angle_enable(true);
        command.steering_angle(5.0f);
        command.timestamp(timestamp_ns);

        send_times_[sequence_id_] = send_time;
        command_writer_->write(&command);

        // 显示 a1time
        std::cout << "a端发送命令: " << timestamp_ns << std::endl;
    }

    void on_data_available(DataReader* reader) override
    {
        SampleInfo info;
        VehicleAck ack;
        
        while (reader->take_next_sample(&ack, &info) == RETCODE_OK)
        {
            if (info.valid_data)
            {
                auto receive_time = std::chrono::high_resolution_clock::now();
                auto timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(receive_time.time_since_epoch()).count();
                
                long long seq_id = ack.sequence_id();
                
                // 显示 a2time
                std::cout << "a端收到确认: " << timestamp_ns << std::endl;

                // 计算延时 a2 - a1
                auto it = send_times_.find(seq_id);
                if (it != send_times_.end())
                {
                    auto send_time = it->second;
                    auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(receive_time - send_time);
                    std::cout << "延时(a2-a1): " << latency.count() / 1e6 << " ms" << std::endl;
                    std::cout << "---" << std::endl;
                    send_times_.erase(it);
                }
            }
        }
    }

    void run()
    {
        std::cout << "开始延时测试\n等待 DDS 发现…" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));

        for (int i = 0; i < 10 && running_; ++i)      // 连续发送 10 条
        {
            send_command();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "等待最后一批确认…" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout << "测试完成" << std::endl;
    }

    void stop() { running_ = false; }
};

LatencyTester* g_tester = nullptr;

void signal_handler(int signum)
{
    if (g_tester) g_tester->stop();
}

int main()
{
    LatencyTester tester;
    g_tester = &tester;
    
    signal(SIGINT, signal_handler);
    
    if (!tester.init())
    {
        std::cout << "初始化失败" << std::endl;
        return -1;
    }
    
    tester.run();
    return 0;
}