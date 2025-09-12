/**
 * @file subscriber.cpp
 * @brief 简化的FastDDS订阅者 - 模拟车端接收消息
 */

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastrtps/xmlparser/XMLProfileManager.h>

// 包含生成的IDL类型
#include "../generated_types/HandShake/HandShake.h"
#include "../generated_types/HandShake/HandShakePubSubTypes.h"
#include "../generated_types/VehicleStatus/VehicleStatus.h"
#include "../generated_types/VehicleStatus/VehicleStatusPubSubTypes.h"
#include "../generated_types/RemoteControl/RemoteControl.h"
#include "../generated_types/RemoteControl/RemoteControlPubSubTypes.h"
#include "../generated_types/DriveState/DriveState.h"
#include "../generated_types/DriveState/DriveStatePubSubTypes.h"
#include "../generated_types/FaultReport/FaultReport.h"
#include "../generated_types/FaultReport/FaultReportPubSubTypes.h"

#include <iostream>
#include <thread>
#include <atomic>
#include <signal.h>
#include "logger_conf/universal_logger.h"
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>

using namespace eprosima::fastdds::dds;
using namespace remote_msgs;

std::atomic<bool> g_running{true};
int g_message_count = 0;

/**
 * @brief 按topic分类的日志记录器
 */
class TopicLogger {
private:
    std::string session_dir_;
    std::map<std::string, std::ofstream> topic_files_;
    
public:
    TopicLogger() {
        // 创建session日志目录
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
        session_dir_ = "logs/fastdds/sub/" + oss.str();
        
        std::filesystem::create_directories(session_dir_);
        std::cout << "日志目录: " << session_dir_ << std::endl;
    }
    
    ~TopicLogger() {
        for (auto& pair : topic_files_) {
            if (pair.second.is_open()) {
                pair.second.close();
            }
        }
    }
    
    void log(const std::string& topic_path, const std::string& message) {
        // 确保topic日志文件存在
        if (topic_files_.find(topic_path) == topic_files_.end()) {
            std::string log_file_path = session_dir_ + "/" + topic_path + ".log";
            
            // 创建子目录
            std::filesystem::path log_path(log_file_path);
            std::filesystem::create_directories(log_path.parent_path());
            
            topic_files_[topic_path].open(log_file_path);
        }
        
        auto& log_file = topic_files_[topic_path];
        if (!log_file.is_open()) return;
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        log_file << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                 << "." << std::setfill('0') << std::setw(3) << ms.count()
                 << "] " << message << std::endl;
        log_file.flush();
    }
    
    std::string get_topic_path(const std::string& topic_name) {
        if (topic_name == "/handshake/request") {
            return "handshake/request";
        } else if (topic_name == "/handshake/response") {
            return "handshake/response";
        } else if (topic_name == "/vehicle/vehicle_status") {
            return "vehicle/vehicle_status";
        } else if (topic_name == "/vehicle/control_cmd") {
            return "vehicle/control_cmd";
        } else if (topic_name == "/vehicle/drive_state") {
            return "vehicle/drive_state";
        } else if (topic_name == "/vehicle/fault_report") {
            return "vehicle/fault_report";
        }
        return "unknown/" + topic_name;
    }
};

// TopicLogger已移除，使用通用日志系统

/**
 * @brief 获取topic对应的日志路径
 */
std::string get_topic_log_path(const std::string& topic_name) {
    if (topic_name == "/handshake/request") return "handshake/request";
    if (topic_name == "/handshake/response") return "handshake/response";
    if (topic_name == "/vehicle/vehicle_status") return "vehicle/vehicle_status";
    if (topic_name == "/vehicle/control_cmd") return "vehicle/control_cmd";
    if (topic_name == "/vehicle/drive_state") return "vehicle/drive_state";
    if (topic_name == "/vehicle/fault_report") return "vehicle/fault_report";
    
    // 默认路径
    std::string clean_name = topic_name;
    if (clean_name.front() == '/') clean_name = clean_name.substr(1);
    std::replace(clean_name.begin(), clean_name.end(), '/', '_');
    return "unknown/" + clean_name;
}

/**
 * @brief 简单的消息监听器
 */
class SimpleListener : public DataReaderListener
{
public:
    void on_data_available(DataReader* reader) override
    {
        std::string topic_name = reader->get_topicdescription()->get_name();
        g_message_count++;
        
        if (topic_name == "/handshake/request") {
            HandshakeRequest sample;
            SampleInfo info;
            if (reader->take_next_sample(&sample, &info) == ReturnCode_t::RETCODE_OK && info.valid_data) {
                std::string msg = "HandshakeRequest: noa_active_request=" + std::to_string(sample.noa_active_request()) + 
                                 ", remote_override_status=" + std::to_string(sample.remote_override_status()) +
                                 ", remote_override_ready=" + std::to_string(sample.remote_override_ready()) +
                                 ", timestamp=" + std::to_string(sample.timestamp());
                std::cout << "[" << g_message_count << "] " << msg << std::endl;
                LOG_BY_TOPIC(get_topic_log_path(topic_name), msg);
            }
        } else if (topic_name == "/handshake/response") {
            HandshakeResponse sample;
            SampleInfo info;
            if (reader->take_next_sample(&sample, &info) == ReturnCode_t::RETCODE_OK && info.valid_data) {
                std::string msg = "HandshakeResponse: noa_active_response=" + std::to_string(sample.noa_active_response()) +
                                 ", remote_override_response=" + std::to_string(sample.remote_override_response()) +
                                 ", current_control_source=" + std::string(sample.current_control_source() ? "true" : "false") +
                                 ", timestamp=" + std::to_string(sample.timestamp());
                std::cout << "[" << g_message_count << "] " << msg << std::endl;
                LOG_BY_TOPIC(get_topic_log_path(topic_name), msg);
            }
        } else if (topic_name == "/vehicle/vehicle_status") {
            VehicleStatus sample;
            SampleInfo info;
            if (reader->take_next_sample(&sample, &info) == ReturnCode_t::RETCODE_OK && info.valid_data) {
                // 构建VehicleStatus字段信息（只显示实际发送的字段，跳过默认值）
                std::ostringstream oss;
                oss << "VehicleStatus: "
                    << "vehicle_id=" << sample.vehicle_id() << ", "
                    << "position=(" << sample.position_longitude() << "," << sample.position_latitude() << "), "
                    << "heading=" << sample.heading() << ", "
                    << "altitude=" << sample.position_altitude() << ", "
                    << "speed=" << sample.speed() << ", "
                    << "yawrate=" << sample.yawrate() << ", "
                    << "gear=" << sample.gear_position() << ", "
                    << "accel=" << sample.acceleration() << ", "
                    << "steering_angle=" << sample.steering_angle() << ", "
                    << "wheel_angle=" << sample.wheel_angle() << ", "
                    << "ebrake=" << sample.ebrake_status() << ", "
                    << "indicators=(L:" << (sample.indicator_left() ? "1" : "0") << ",R:" << (sample.indicator_right() ? "1" : "0") << "), "
                    << "power_mode=" << static_cast<int>(sample.power_mode()) << ", "
                    << "control_mode=" << sample.control_mode() << ", "
                    << "timestamp=" << sample.timestamp();
                
                // 只有非零值才显示这些可选字段
                if (sample.high_beam_status() || sample.low_beam_status() || sample.emergency_light_status()) {
                    oss << ", lights=(high:" << (sample.high_beam_status() ? "1" : "0") 
                        << ",low:" << (sample.low_beam_status() ? "1" : "0") 
                        << ",emergency:" << (sample.emergency_light_status() ? "1" : "0") << ")";
                }
                if (sample.loc_lane_id() != 0 || sample.cruise_lane_id() != 0 || sample.local_lanes().size() > 0) {
                    oss << ", lanes=(loc:" << sample.loc_lane_id() 
                        << ",cruise:" << sample.cruise_lane_id() 
                        << ",count:" << sample.local_lanes().size() << ")";
                }
                if (sample.driving_efficiency() != 0) {
                    oss << ", efficiency=" << sample.driving_efficiency();
                }
                std::string msg = oss.str();
                std::cout << "[" << g_message_count << "] " << msg << std::endl;
                LOG_BY_TOPIC(get_topic_log_path(topic_name), msg);
            }
        } else if (topic_name == "/vehicle/control_cmd") {
            ControlCmd sample;
            SampleInfo info;
            if (reader->take_next_sample(&sample, &info) == ReturnCode_t::RETCODE_OK && info.valid_data) {
                // 构建完整的ControlCmd字段信息
                std::ostringstream oss;
                oss << "ControlCmd: "
                    << "steering_angle_enable=" << (sample.steering_angle_enable() ? "true" : "false") << ", "
                    << "steering_angle=" << sample.steering_angle() << ", "
                    << "target_acceleration_enable=" << (sample.target_acceleration_enable() ? "true" : "false") << ", "
                    << "target_acceleration=" << sample.target_acceleration() << ", "
                    << "indicator_left_enable=" << (sample.indicator_left_enable() ? "true" : "false") << ", "
                    << "indicator_left=" << (sample.indicator_left() ? "true" : "false") << ", "
                    << "indicator_right_enable=" << (sample.indicator_right_enable() ? "true" : "false") << ", "
                    << "indicator_right=" << (sample.indicator_right() ? "true" : "false") << ", "
                    << "gear_position_enable=" << (sample.gear_position_enable() ? "true" : "false") << ", "
                    << "gear_position=" << sample.gear_position() << ", "
                    << "ebrake_status_enable=" << (sample.ebrake_status_enable() ? "true" : "false") << ", "
                    << "ebrake_status=" << sample.ebrake_status() << ", "
                    << "lane_change=" << sample.lane_change() << ", "
                    << "timestamp=" << sample.timestamp();
                std::string msg = oss.str();
                std::cout << "[" << g_message_count << "] " << msg << std::endl;
                LOG_BY_TOPIC(get_topic_log_path(topic_name), msg);
            }
        } else if (topic_name == "/vehicle/drive_state") {
            StateMachineState sample;
            SampleInfo info;
            if (reader->take_next_sample(&sample, &info) == ReturnCode_t::RETCODE_OK && info.valid_data) {
                std::string msg = "DriveState: driving_state=" + std::to_string(sample.driving_state());
                std::cout << "[" << g_message_count << "] " << msg << std::endl;
                LOG_BY_TOPIC(get_topic_log_path(topic_name), msg);
            }
        } else if (topic_name == "/vehicle/fault_report") {
            FaultReport sample;
            SampleInfo info;
            if (reader->take_next_sample(&sample, &info) == ReturnCode_t::RETCODE_OK && info.valid_data) {
                std::string fault_list = "[";
                const auto& faults = sample.fault_type();
                for (size_t i = 0; i < faults.size(); ++i) {
                    if (i > 0) fault_list += ",";
                    fault_list += std::to_string(faults[i]);
                }
                fault_list += "]";
                
                std::string msg = "FaultReport: fault_count=" + std::to_string(faults.size()) + 
                                 ", fault_types=" + fault_list;
                std::cout << "[" << g_message_count << "] " << msg << std::endl;
                LOG_BY_TOPIC(get_topic_log_path(topic_name), msg);
            }
        }
    }
};

void signal_handler(int signal) {
    std::cout << "\n收到停止信号，退出程序..." << std::endl;
    g_running = false;
}

int main()
{
    std::cout << "=== 简化版FastDDS订阅者 - 车端消息接收测试 ===" << std::endl;
    std::cout << "按 Ctrl+C 退出\n" << std::endl;
    
    // 初始化通用日志系统
    initUniversalLogger("subscriber");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // 加载XML配置
        bool xml_loaded = (eprosima::fastrtps::xmlparser::XMLProfileManager::loadXMLFile("subscriber.xml") == eprosima::fastrtps::xmlparser::XMLP_ret::XML_OK);
        
        // 创建参与者
        DomainParticipant* participant = nullptr;
        if (xml_loaded) {
            std::cout << "使用subscriber.xml配置" << std::endl;
            participant = DomainParticipantFactory::get_instance()->create_participant_with_profile(0, "sub_profile");
        } else {
            std::cout << "使用默认配置" << std::endl;
            participant = DomainParticipantFactory::get_instance()->create_participant(0, PARTICIPANT_QOS_DEFAULT);
        }
        
        if (!participant) {
            std::cerr << "创建参与者失败" << std::endl;
            return 1;
        }
        
        // 创建订阅者
        Subscriber* subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
        if (!subscriber) {
            std::cerr << "创建订阅者失败" << std::endl;
            return 1;
        }
        
        // 创建监听器
        SimpleListener listener;
        
        // 订阅5个主题  
        std::vector<std::string> topics = {"/handshake/request", "/handshake/response", "/vehicle/vehicle_status", "/vehicle/control_cmd", "/vehicle/drive_state", "/vehicle/fault_report"};
        std::vector<TypeSupport> types = {
            TypeSupport(new HandshakeRequestPubSubType()),
            TypeSupport(new HandshakeResponsePubSubType()),
            TypeSupport(new VehicleStatusPubSubType()),
            TypeSupport(new ControlCmdPubSubType()),
            TypeSupport(new StateMachineStatePubSubType()),
            TypeSupport(new FaultReportPubSubType())
        };
        
        for (size_t i = 0; i < topics.size(); ++i) {
            participant->register_type(types[i]);
            Topic* topic = participant->create_topic(topics[i], types[i].get_type_name(), TOPIC_QOS_DEFAULT);
            
            DataReaderQos reader_qos;
            reader_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
            reader_qos.data_sharing().off();
            
            // 设置历史缓冲区大小以支持136字节的VehicleStatus消息
            reader_qos.history().depth = 20;
            reader_qos.resource_limits().max_samples = 200;
            reader_qos.resource_limits().max_instances = 20;
            reader_qos.resource_limits().max_samples_per_instance = 20;
            
            DataReader* reader = subscriber->create_datareader(topic, reader_qos, &listener);
            if (reader) {
                std::cout << "订阅成功: " << topics[i] << std::endl;
            }
        }
        
        std::cout << "\n开始接收消息...\n" << std::endl;
        
        // 主循环
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "\n总计接收消息: " << g_message_count << " 条" << std::endl;
        
        // 清理
        DomainParticipantFactory::get_instance()->delete_participant(participant);
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }
    
    cleanupUniversalLogger();
    return 0;
}