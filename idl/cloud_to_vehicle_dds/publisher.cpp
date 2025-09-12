#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <signal.h>
#include <iomanip>
#include <limits>

// 包含生成的头文件
#include "CloudToVehiclePubSubTypes.hpp"

using namespace eprosima::fastdds::dds;

// 全局变量
volatile bool running_ = true;
volatile bool connected_ = false;

// 信号处理函数
void signal_handler(int signum) {
    std::cout << "\n接收到信号 " << signum << "，正在停止程序..." << std::endl;
    running_ = false;
}

// 自定义DataWriter监听器
class CustomDataWriterListener : public eprosima::fastdds::dds::DataWriterListener {
public:
    void on_publication_matched(DataWriter* writer, const PublicationMatchedStatus& info) override {
        if (info.current_count_change == 1) {
            std::cout << "检测到订阅者连接" << std::endl;
            connected_ = true;
        } else if (info.current_count_change == -1) {
            std::cout << "订阅者断开连接" << std::endl;
            connected_ = false;
        }
    }
};

// 获取当前时间戳（毫秒）
int64_t get_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

// 获取当前时间戳（微秒）
int64_t get_timestamp_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

// 精确sleep函数 - 动态调整等待时间以保持精确的100Hz频率
void precise_sleep_us(int64_t microseconds) {
    auto start = std::chrono::high_resolution_clock::now();
    auto target = start + std::chrono::microseconds(microseconds);
    
    while (std::chrono::high_resolution_clock::now() < target) {
        // 使用yield让出CPU时间片，减少CPU占用
        std::this_thread::yield();
    }
}

// 获取格式化的时间戳字符串
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

// 打印详细的JSON格式消息
void print_detailed_json(const CloudToVehicle& data, int count, int64_t a1_timestamp) {
    std::cout << "[" << get_formatted_timestamp() << "] 发送第 " << count << " 条消息:" << std::endl;
    std::cout << "a1_timestamp: " << a1_timestamp << " ms" << std::endl;
    std::cout << "{" << std::endl;
    std::cout << "  \"sequence_id\": " << data.sequence_id() << "," << std::endl;
    std::cout << "  \"steering_angle_enable\": " << (data.steering_angle_enable() ? "true" : "false") << "," << std::endl;
    std::cout << "  \"steering_angle\": " << data.steering_angle() << "," << std::endl;
    std::cout << "  \"target_acceleration_enable\": " << (data.target_acceleration_enable() ? "true" : "false") << "," << std::endl;
    std::cout << "  \"target_acceleration\": " << data.target_acceleration() << "," << std::endl;
    std::cout << "  \"indicator_left_enable\": " << (data.indicator_left_enable() ? "true" : "false") << "," << std::endl;
    std::cout << "  \"indicator_left\": " << (data.indicator_left() ? "true" : "false") << "," << std::endl;
    std::cout << "  \"indicator_right_enable\": " << (data.indicator_right_enable() ? "true" : "false") << "," << std::endl;
    std::cout << "  \"indicator_right\": " << (data.indicator_right() ? "true" : "false") << "," << std::endl;
    std::cout << "  \"gear_position_enable\": " << (data.gear_position_enable() ? "true" : "false") << "," << std::endl;
    std::cout << "  \"gear_position\": " << data.gear_position() << "," << std::endl;
    std::cout << "  \"ebrake_status_enable\": " << (data.ebrake_status_enable() ? "true" : "false") << "," << std::endl;
    std::cout << "  \"ebrake_status\": " << data.ebrake_status() << "," << std::endl;
    std::cout << "  \"remote_drive_ready\": " << (data.remote_drive_ready() ? "true" : "false") << "," << std::endl;
    std::cout << "  \"remote_drive_takeover_request\": " << (data.remote_drive_takeover_request() ? "true" : "false") << "," << std::endl;
    std::cout << "  \"timestamp\": " << data.timestamp() << std::endl;
    std::cout << "}" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
}

// === 主程序类 ===
class DataPublisher {
private:
    DomainParticipant* participant_;
    Publisher* publisher_;
    Topic* topic_;
    DataWriter* writer_;
    TypeSupport type_;
    CustomDataWriterListener* listener_;

public:
    DataPublisher() : participant_(nullptr), publisher_(nullptr), 
                     topic_(nullptr), writer_(nullptr), listener_(nullptr) {
        type_ = TypeSupport(new CloudToVehiclePubSubType());
        listener_ = new CustomDataWriterListener();
    }

    ~DataPublisher() {
        if (writer_ != nullptr) {
            publisher_->delete_datawriter(writer_);
        }
        if (publisher_ != nullptr) {
            participant_->delete_publisher(publisher_);
        }
        if (topic_ != nullptr) {
            participant_->delete_topic(topic_);
        }
        DomainParticipantFactory::get_instance()->delete_participant(participant_);
        if (listener_ != nullptr) {
            delete listener_;
        }
    }

    bool init() {
        // 创建参与者
        participant_ = DomainParticipantFactory::get_instance()->create_participant(
            0, PARTICIPANT_QOS_DEFAULT);
        if (participant_ == nullptr) {
            return false;
        }

        // 注册类型
        type_.register_type(participant_);

        // 创建发布者
        publisher_ = participant_->create_publisher(PUBLISHER_QOS_DEFAULT, nullptr);
        if (publisher_ == nullptr) {
            return false;
        }

        // 创建主题
        topic_ = participant_->create_topic("CloudToVehicle", "CloudToVehicle", TOPIC_QOS_DEFAULT);
        if (topic_ == nullptr) {
            return false;
        }

        // 创建数据写入器（带监听器）
        writer_ = publisher_->create_datawriter(topic_, DATAWRITER_QOS_DEFAULT, listener_);
        if (writer_ == nullptr) {
            return false;
        }

        std::cout << "发布者初始化成功" << std::endl;
        std::cout << "主题名称: CloudToVehicle" << std::endl;
        std::cout << "域ID: 0" << std::endl;
        return true;
    }

    void run() {
    std::cout << "等待订阅者连接..." << std::endl;
    
    // 等待订阅者连接
    int wait_count = 0;
    while (!connected_ && running_ && wait_count < 30) {  // 最多等待30秒
        std::cout << "等待订阅者连接... (" << (30 - wait_count) << "秒)" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        wait_count++;
    }
    
    if (!connected_) {
        std::cout << "未检测到订阅者连接，程序退出" << std::endl;
        return;
    }
    
    std::cout << "检测到订阅者连接，开始发送数据..." << std::endl;
    std::cout << "目标频率: 100Hz (每10ms发送一次)" << std::endl;
    std::cout << "时间戳精度: 微秒级别" << std::endl;

    int count = 0;
    const int64_t TARGET_INTERVAL_US = 10000;  // 10ms = 10000微秒
    
    // 记录开始时间
    int64_t start_time = get_timestamp_us();
    int64_t last_send_time = start_time;
    int64_t last_a1_timestamp = 0;  // 记录上一次的a1_timestamp
    
    // 统计变量（用于内部计算，不打印）
    int64_t total_send_time_us = 0;
    int64_t max_send_time_us = 0;
    int64_t min_send_time_us = std::numeric_limits<int64_t>::max();
    
    while (running_ && connected_) {
        // 记录进入函数的时间戳（微秒级别）
        int64_t function_start_time = get_timestamp_us();
        
        // 直接创建数据对象，不使用command概念
        CloudToVehicle data;
        
        // 设置数据
        data.sequence_id(count + 1);
        data.steering_angle_enable(true);
        data.steering_angle(0.0f);
        data.target_acceleration_enable(true);
        data.target_acceleration(0.0f);
        data.indicator_left_enable(false);
        data.indicator_left(false);
        data.indicator_right_enable(false);
        data.indicator_right(false);
        data.gear_position_enable(false);
        data.gear_position(0);
        data.ebrake_status_enable(false);
        data.ebrake_status(0);
        data.remote_drive_ready(false);
        data.remote_drive_takeover_request(false);
        
        // 获取发送前的时间戳（毫秒级别，用于消息内容）
        int64_t a1_timestamp = get_timestamp_ms();
        
        // 设置消息中的时间戳
        data.timestamp(a1_timestamp);
        
        // 发送数据
        ReturnCode_t ret = writer_->write(&data);
        
        // 记录发送完成的时间戳
        int64_t function_end_time = get_timestamp_us();
        
        if (ret == RETCODE_OK) {
            count++;
            
            // 计算发送耗时（微秒）
            int64_t send_time_us = function_end_time - function_start_time;
            
            // 更新统计信息（内部使用，不打印）
            total_send_time_us += send_time_us; // total_send_time_us = total_send_time_us + send_time_us;
            max_send_time_us = std::max(max_send_time_us, send_time_us);
            min_send_time_us = std::min(min_send_time_us, send_time_us);
            
            // // 计算实际间隔（微秒）
            // int64_t actual_interval_us = function_end_time - last_send_time;
            // last_send_time = function_end_time;
            
            // 计算a1_timestamp的差值（毫秒）
            int64_t a1_interval_ms = 0;
            if (last_a1_timestamp > 0) {
                a1_interval_ms = a1_timestamp - last_a1_timestamp;
            }
            last_a1_timestamp = a1_timestamp;
            
            // 计算需要等待的时间（动态调整以保持精确的100Hz频率）
            int64_t sleep_time_us = TARGET_INTERVAL_US - send_time_us;
            
            // 确保sleep时间不为负数
            if (sleep_time_us < 0) {
                sleep_time_us = 0;
                std::cout << "警告: 发送耗时 " << send_time_us << " 微秒超过目标间隔 " 
                         << TARGET_INTERVAL_US << " 微秒" << std::endl;
            }
            
            // 输出：显示基本信息 + 发送间隔
            if (count == 1) {
                // 第一条消息不显示间隔
                std::cout << "[" << get_formatted_timestamp() << "] 发送第 " << count 
                         << " 条消息 | a1_timestamp: " << a1_timestamp << " ms" << std::endl;
            } else {
                // 后续消息显示间隔
                std::cout << "[" << get_formatted_timestamp() << "] 发送第 " << count 
                         << " 条消息 | a1_timestamp: " << a1_timestamp << " ms"
                         << " | 间隔: " << a1_interval_ms << " ms" << std::endl;
            }
            
            // 每10条消息显示详细JSON
            if (count % 10 == 0) {
                print_detailed_json(data, count, a1_timestamp);
            }
            
            // 精确等待（动态调整的sleep时间）
            if (sleep_time_us > 0) {
                precise_sleep_us(sleep_time_us);
            }
        } else {
            std::cerr << "发送失败，错误代码: " << ret << std::endl;
            // 即使发送失败也要等待，保持频率
            precise_sleep_us(TARGET_INTERVAL_US);
        }
    }
}};


// === 主函数 ===
int main() {
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "=== 数据发布者程序 ===" << std::endl;
    std::cout << "发送频率: 100Hz (每10ms发送一次)" << std::endl;
    std::cout << "时间戳精度: 微秒级别" << std::endl;
    std::cout << "按 Ctrl+C 停止程序" << std::endl;

    DataPublisher publisher;
    
    if (!publisher.init()) {
        std::cerr << "初始化失败！" << std::endl;
        return -1;
    }

    publisher.run();
    
    std::cout << "程序结束" << std::endl;
    return 0;
}