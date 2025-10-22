/**
 * @file subscriber.cpp
 * @brief 精简版FastDDS订阅者 - 模拟车端接收消息
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
#include <climits>
#include <cfloat>
#include <sstream>
#include <algorithm>
#include <map>
#include "logger_conf/universal_logger.h"

using namespace eprosima::fastdds::dds;
using namespace remote_msgs;

std::atomic<bool> g_running{true};
int g_message_count = 0;

// 简化的状态跟踪 - 使用字符串hash来检测变化
std::map<std::string, std::string> g_last_messages;

std::string get_topic_log_path(const std::string& topic_name) {
    if (topic_name.find("/handshake/request") == 0) return "handshake/request";
    if (topic_name.find("/handshake/response") == 0) return "handshake/response";
    if (topic_name.find("/vehicle/vehicle_status") == 0) return "vehicle/vehicle_status";
    if (topic_name.find("/vehicle/control_cmd") == 0) return "vehicle/control_cmd";
    if (topic_name.find("/vehicle/drive_state") == 0) return "vehicle/drive_state";
    if (topic_name.find("/vehicle/fault_report") == 0) return "vehicle/fault_report";
    
    std::string clean_name = topic_name;
    if (clean_name.front() == '/') clean_name = clean_name.substr(1);
    std::replace(clean_name.begin(), clean_name.end(), '/', '_');
    return "unknown/" + clean_name;
}

// 通用的消息处理函数
void process_message(const std::string& topic_name, const std::string& msg, bool print_to_terminal = true) {
    // 总是记录日志
    LOG_BY_TOPIC(get_topic_log_path(topic_name), msg);
    
    // 如果不需要打印到终端，直接返回
    if (!print_to_terminal) {
        return;
    }
    
    // 提取不包含timestamp的内容进行比较
    std::string key_content = msg;
    size_t timestamp_pos = key_content.find(", timestamp=");
    if (timestamp_pos != std::string::npos) {
        key_content = key_content.substr(0, timestamp_pos);
    }
    
    bool is_first = g_last_messages.find(topic_name) == g_last_messages.end();
    bool has_changed = is_first || g_last_messages[topic_name] != key_content;
    
    if (has_changed) {
        std::string action = is_first ? "[INIT]" : "[UPDATE]";
        std::cout << "[" << g_message_count << "] " << action << " " << msg << std::endl;
        g_last_messages[topic_name] = key_content;
    }
}

class SimpleListener : public DataReaderListener {
public:
    void on_data_available(DataReader* reader) override {
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
                process_message(topic_name, msg);
            }
        } else if (topic_name == "/handshake/response") {
            HandshakeResponse sample;
            SampleInfo info;
            if (reader->take_next_sample(&sample, &info) == ReturnCode_t::RETCODE_OK && info.valid_data) {
                std::string msg = "HandshakeResponse: noa_active_response=" + std::to_string(sample.noa_active_response()) +
                                 ", remote_override_response=" + std::to_string(sample.remote_override_response()) +
                                 ", current_control_source=" + std::string(sample.current_control_source() ? "true" : "false") +
                                 ", timestamp=" + std::to_string(sample.timestamp());
                process_message(topic_name, msg);
            }
        } else if (topic_name == "/vehicle/vehicle_status") {
            VehicleStatus sample;
            SampleInfo info;
            if (reader->take_next_sample(&sample, &info) == ReturnCode_t::RETCODE_OK && info.valid_data) {
                std::ostringstream oss;
                oss << "VehicleStatus: "
                    << "vehicle_id=" << sample.vehicle_id() << ", "
                    << "position=(" << sample.position_longitude() << "," << sample.position_latitude() << "), "
                    << "heading=" << sample.heading() << ", speed=" << sample.speed() << ", "
                    << "yawrate=" << sample.yawrate() << ", gear=" << sample.gear_position() << ", "
                    << "accel=" << sample.acceleration() << ", steering_angle=" << sample.steering_angle() << ", "
                    << "ebrake=" << sample.ebrake_status() << ", "
                    << "indicators=(L:" << (sample.indicator_left() ? "1" : "0") << ",R:" << (sample.indicator_right() ? "1" : "0") << "), "
                    << "control_mode=" << sample.control_mode() << ", timestamp=" << sample.timestamp();
                process_message(topic_name, oss.str(), false);  // 不打印到终端，只记录日志
            }
        } else if (topic_name == "/vehicle/control_cmd") {
            ControlCmd sample;
            SampleInfo info;
            if (reader->take_next_sample(&sample, &info) == ReturnCode_t::RETCODE_OK && info.valid_data) {
                std::ostringstream oss;
                oss << "ControlCmd: "
                    << "steering_angle_enable=" << (sample.steering_angle_enable() ? "true" : "false") << ", "
                    << "steering_angle=" << sample.steering_angle() << ", "
                    << "target_acceleration_enable=" << (sample.target_acceleration_enable() ? "true" : "false") << ", "
                    << "target_acceleration=" << sample.target_acceleration() << ", "
                    << "gear_position_enable=" << (sample.gear_position_enable() ? "true" : "false") << ", "
                    << "gear_position=" << sample.gear_position() << ", "
                    << "ebrake_status_enable=" << (sample.ebrake_status_enable() ? "true" : "false") << ", "
                    << "ebrake_status=" << sample.ebrake_status() << ", "
                    << "lane_change=" << sample.lane_change() << ", "
                    << "timestamp=" << sample.timestamp();
                process_message(topic_name, oss.str());
            }
        } else if (topic_name == "/vehicle/drive_state") {
            StateMachineState sample;
            SampleInfo info;
            if (reader->take_next_sample(&sample, &info) == ReturnCode_t::RETCODE_OK && info.valid_data) {
                std::string msg = "DriveState: driving_state=" + std::to_string(sample.driving_state());
                process_message(topic_name, msg);
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
                process_message(topic_name, msg, false);  // 不打印到终端，只记录日志
            }
        } else if (topic_name == "/handshake/request") {
            HandshakeRequest sample;
            SampleInfo info;
            if (reader->take_next_sample(&sample, &info) == ReturnCode_t::RETCODE_OK && info.valid_data) {
                std::string msg = "HandshakeRequest: noa_active_request=" + std::to_string(sample.noa_active_request()) + 
                                 ", remote_override_status=" + std::to_string(sample.remote_override_status()) +
                                 ", remote_override_ready=" + std::to_string(sample.remote_override_ready()) +
                                 ", timestamp=" + std::to_string(sample.timestamp());
                process_message(topic_name, msg, false);  // 不打印到终端，只记录日志
            }
        }
    }
};

void signal_handler(int signal) {
    std::cout << "\n收到停止信号，退出程序..." << std::endl;
    g_running = false;
}

int main() {
    std::cout << "=== 精简版FastDDS订阅者 - 车端消息接收测试 ===" << std::endl;
    std::cout << "按 Ctrl+C 退出\n" << std::endl;
    
    initUniversalLogger("subscriber");
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // 加载XML配置
        bool xml_loaded = (eprosima::fastrtps::xmlparser::XMLProfileManager::loadXMLFile("subscriber.xml") == 
                          eprosima::fastrtps::xmlparser::XMLP_ret::XML_OK);
        
        // 创建参与者
        DomainParticipant* participant = xml_loaded ? 
            DomainParticipantFactory::get_instance()->create_participant_with_profile(0, "sub_profile") :
            DomainParticipantFactory::get_instance()->create_participant(0, PARTICIPANT_QOS_DEFAULT);
        
        if (!participant) {
            std::cerr << "创建参与者失败" << std::endl;
            return 1;
        }
        
        // 创建订阅者和监听器
        Subscriber* subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
        if (!subscriber) {
            std::cerr << "创建订阅者失败" << std::endl;
            return 1;
        }
        
        SimpleListener listener;
        
        // 订阅所有主题
        std::vector<std::pair<std::string, TypeSupport>> topics = {
            {"/handshake/request", TypeSupport(new HandshakeRequestPubSubType())},
            {"/handshake/response", TypeSupport(new HandshakeResponsePubSubType())},
            {"/vehicle/vehicle_status", TypeSupport(new VehicleStatusPubSubType())},
            {"/vehicle/control_cmd", TypeSupport(new ControlCmdPubSubType())},
            {"/vehicle/drive_state", TypeSupport(new StateMachineStatePubSubType())},
            {"/vehicle/fault_report", TypeSupport(new FaultReportPubSubType())}
        };
        
        for (const auto& [topic_name, type_support] : topics) {
            participant->register_type(type_support);
            Topic* topic = participant->create_topic(topic_name, type_support.get_type_name(), TOPIC_QOS_DEFAULT);
            
            DataReaderQos reader_qos;
            reader_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
            reader_qos.data_sharing().off();
            reader_qos.history().depth = 20;
            reader_qos.resource_limits().max_samples = 200;
            
            DataReader* reader = subscriber->create_datareader(topic, reader_qos, &listener);
            if (reader) {
                std::cout << "订阅成功: " << topic_name << std::endl;
            }
        }
        
        std::cout << "\n开始接收消息...\n" << std::endl;
        
        // 主循环
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        std::cout << "\n总计接收消息: " << g_message_count << " 条" << std::endl;
        DomainParticipantFactory::get_instance()->delete_participant(participant);
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }
    
    cleanupUniversalLogger();
    return 0;
}