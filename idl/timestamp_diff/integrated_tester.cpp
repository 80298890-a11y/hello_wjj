// integrated_tester.cpp - 集成测试器
// 功能：同时处理 Topic 1 (CloudToVehicle) 和 Topic 2 (VehicleAck)
// 参考 timestamp_diff_sender.cpp 的实现方式

#include <iostream>
#include <chrono>
#include <map>
#include <thread>
#include <csignal>
#include <iomanip>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>

// === Topic 1 头文件 ===
#include "../cloud_to_vehicle_dds/CloudToVehicle.hpp"
#include "../cloud_to_vehicle_dds/CloudToVehiclePubSubTypes.hpp"

// === Topic 2 头文件 ===
#include "VehicleAck.hpp"
#include "VehicleAckPubSubTypes.hpp"

using namespace eprosima::fastdds::dds;

class IntegratedTester : public DataReaderListener
{
private:
    DomainParticipant* participant_;
    
    // Topic 1 相关（发布）
    TypeSupport cloud_to_vehicle_type_;
    Topic* cloud_to_vehicle_topic_;
    Publisher* cloud_to_vehicle_publisher_;
    DataWriter* cloud_to_vehicle_writer_;
    
    // Topic 2 相关（订阅）
    TypeSupport vehicle_ack_type_;
    Topic* vehicle_ack_topic_;
    Subscriber* vehicle_ack_subscriber_;
    DataReader* vehicle_ack_reader_;
    
    long long sequence_id_;
    std::map<long long, int64_t> send_times_;  // 存储发送时间戳 a1
    bool running_;

public:
    IntegratedTester() : participant_(nullptr), 
                        cloud_to_vehicle_topic_(nullptr), cloud_to_vehicle_publisher_(nullptr), cloud_to_vehicle_writer_(nullptr),
                        vehicle_ack_topic_(nullptr), vehicle_ack_subscriber_(nullptr), vehicle_ack_reader_(nullptr),
                        sequence_id_(0), running_(true) {
        std::cout << "IntegratedTester 初始化" << std::endl;
    }

    ~IntegratedTester() {
        if (participant_ != nullptr) {
            participant_->delete_contained_entities();
            DomainParticipantFactory::get_instance()->delete_participant(participant_);
        }
    }

    bool init()
    {
        // 1. 创建 DDS 参与者
        DomainParticipantFactory* factory = DomainParticipantFactory::get_instance();
        participant_ = factory->create_participant(0, PARTICIPANT_QOS_DEFAULT);
        if (participant_ == nullptr) return false;

        // 2. 注册 Topic 1 类型
        cloud_to_vehicle_type_ = TypeSupport(new CloudToVehiclePubSubType());
        cloud_to_vehicle_type_.register_type(participant_);

        // 3. 注册 Topic 2 类型
        vehicle_ack_type_ = TypeSupport(new VehicleAckPubSubType());
        vehicle_ack_type_.register_type(participant_);

        // 4. 创建 Topic 1 发布者
        cloud_to_vehicle_publisher_ = participant_->create_publisher(PUBLISHER_QOS_DEFAULT);
        if (cloud_to_vehicle_publisher_ == nullptr) return false;

        cloud_to_vehicle_topic_ = participant_->create_topic("CloudToVehicle", 
                                                           "CloudToVehicle", TOPIC_QOS_DEFAULT);
        if (cloud_to_vehicle_topic_ == nullptr) return false;

        cloud_to_vehicle_writer_ = cloud_to_vehicle_publisher_->create_datawriter(cloud_to_vehicle_topic_, 
                                                                                 DATAWRITER_QOS_DEFAULT);
        if (cloud_to_vehicle_writer_ == nullptr) return false;

        // 5. 创建 Topic 2 订阅者
        vehicle_ack_subscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
        if (vehicle_ack_subscriber_ == nullptr) return false;

        vehicle_ack_topic_ = participant_->create_topic("VehicleAck", 
                                                      "VehicleAck", TOPIC_QOS_DEFAULT);
        if (vehicle_ack_topic_ == nullptr) return false;

        vehicle_ack_reader_ = vehicle_ack_subscriber_->create_datareader(vehicle_ack_topic_, 
                                                                        DATAREADER_QOS_DEFAULT, this);
        if (vehicle_ack_reader_ == nullptr) return false;

        std::cout << "集成测试器初始化完成" << std::endl;
        std::cout << "发布 Topic 1: CloudToVehicle" << std::endl;
        std::cout << "订阅 Topic 2: VehicleAck" << std::endl;
        std::cout << "域ID: 0" << std::endl;
        return true;
    }

    // === 获取毫秒级别时间戳的函数 ===
    int64_t get_timestamp_ms() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
    }

    // === 获取格式化的时间戳字符串 ===
    std::string get_formatted_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    // === 发送 Topic 1 数据 ===
    void send_cloud_to_vehicle()
    {
        sequence_id_++;
        
        // 获取发送时间戳 a1
        int64_t a1_timestamp = get_timestamp_ms();
        std::string a1_formatted = get_formatted_timestamp();
        
        CloudToVehicle command;
        command.sequence_id(sequence_id_);
        command.steering_angle_enable(true);
        command.steering_angle(0.0f);
        command.target_acceleration_enable(true);
        command.target_acceleration(0.0f);
        command.indicator_left_enable(false);
        command.indicator_left(false);
        command.indicator_right_enable(false);
        command.indicator_right(false);
        command.gear_position_enable(false);
        command.gear_position(0);
        command.ebrake_status_enable(false);
        command.ebrake_status(0);
        command.remote_drive_ready(true);
        command.remote_drive_takeover_request(false);
        command.timestamp(a1_timestamp);

        // 存储发送时间戳
        send_times_[sequence_id_] = a1_timestamp;
        
        // 发送数据
        ReturnCode_t ret = cloud_to_vehicle_writer_->write(&command);
        if (ret == RETCODE_OK) {
            std::cout << "[" << a1_formatted << "] 【发送 Topic 1】消息 #" << sequence_id_ 
                     << " | 发送时间戳 a1: " << a1_timestamp << " 毫秒" << std::endl;
        }
    }

    // === Topic 2 数据到达回调函数 ===
    void on_data_available(DataReader* reader) override
    {
        SampleInfo info;
        VehicleAck ack_data;
        
        while (reader->take_next_sample(&ack_data, &info) == RETCODE_OK)
        {
            if (info.valid_data)
            {
                // 获取接收时间戳 a2
                int64_t a2_timestamp = get_timestamp_ms();
                std::string a2_formatted = get_formatted_timestamp();
                
                long long seq_id = ack_data.sequence_id();
                
                std::cout << "[" << a2_formatted << "] 【接收 Topic 2】消息 #" << seq_id 
                         << " | 接收时间戳 a2: " << a2_timestamp << " 毫秒" << std::endl;

                // 计算延时 a2 - a1
                auto it = send_times_.find(seq_id);
                if (it != send_times_.end())
                {
                    int64_t a1_time = it->second;
                    int64_t latency_ms = a2_timestamp - a1_time;
                    std::cout << "延时(a2-a1): " << latency_ms << " 毫秒" << std::endl;
                    std::cout << "---" << std::endl;
                    send_times_.erase(it);
                }
            }
        }
    }

    void run()
    {
        std::cout << "开始集成测试..." << std::endl;
        std::cout << "等待 DDS 网络发现..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));

        int count = 0;
        const int MAX_MESSAGES = 10;
        
        while (running_ && count < MAX_MESSAGES)
        {
            send_cloud_to_vehicle();
            count++;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "等待最后一批确认..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout << "测试完成，共发送 " << count << " 条消息" << std::endl;
    }

    void stop() { 
        running_ = false; 
        std::cout << "收到停止信号" << std::endl;
    }
};

// === 全局变量和信号处理 ===
IntegratedTester* g_tester = nullptr;

void signal_handler(int signum)
{
    std::cout << "\n接收到信号 " << signum << "，正在停止..." << std::endl;
    if (g_tester) g_tester->stop();
}

// === 主函数 ===
int main()
{
    std::cout << "=== 集成测试器程序 ===" << std::endl;
    std::cout << "功能：同时处理 Topic 1 (CloudToVehicle) 和 Topic 2 (VehicleAck)" << std::endl;
    std::cout << "时间戳记录：a1(发送), a2(接收)" << std::endl;
    std::cout << "按 Ctrl+C 停止程序" << std::endl;
    std::cout << "=======================================" << std::endl;
    
    IntegratedTester tester;
    g_tester = &tester;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (!tester.init())
    {
        std::cout << "错误：DDS 初始化失败，程序退出" << std::endl;
        return -1;
    }
    
    tester.run();
    
    std::cout << "程序正常结束" << std::endl;
    return 0;
}