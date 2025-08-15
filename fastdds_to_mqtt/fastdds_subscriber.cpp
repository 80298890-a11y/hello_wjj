/**
 * FastDDS订阅者
 * 功能：接收CloudToVehicle消息并转发到MQTT
 */
 #include <fastdds/dds/domain/DomainParticipant.hpp>
 #include <fastdds/dds/subscriber/Subscriber.hpp>
 #include <fastdds/dds/topic/Topic.hpp>
 #include <fastdds/dds/subscriber/DataReader.hpp>
 #include <fastdds/dds/subscriber/DataReaderListener.hpp>
 #include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
 #include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/core/status/StatusMask.hpp>
#include <fastdds/dds/core/policy/QosPolicies.hpp>
#include <fastdds/dds/core/status/BaseStatus.hpp>

#include <mosquitto.h>
 #include <json/json.h>
 #include "CloudToVehicle.hpp"
 #include "CloudToVehiclePubSubTypes.hpp"
 
 #include <chrono>
 #include <thread>
 #include <fstream>
#include <sstream>
#include <iomanip>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
 
 using namespace eprosima::fastdds::dds;
using namespace std::chrono;
 
 class CloudToVehicleSubscriber {
 private:
     DomainParticipant* participant_;
     Subscriber* subscriber_;
     Topic* topic_;
     DataReader* reader_;
     TypeSupport type_;
     
     // MQTT相关
     struct mosquitto* mosq_;
     const char* mqtt_host_ = "hellorobotaxi.cn";
     int mqtt_port_ = 11883;
     const char* mqtt_topic_ = "cloud/vehicle/control";
     const char* client_id_ = "mqttx_ae1f7a37";
     
         // 日志相关
    std::ofstream log_file_;
    std::string log_dir_;
    std::queue<std::string> log_queue_;
    std::mutex log_mutex_;
    std::condition_variable log_cv_;
    std::thread log_thread_;
    bool log_running_;
 
     class SubListener : public DataReaderListener {
     private:
         CloudToVehicleSubscriber* subscriber_;
 
     public:
         SubListener(CloudToVehicleSubscriber* sub) : subscriber_(sub) {}
 
                 void on_data_available(DataReader* reader) override {
            CloudToVehicle data;
            SampleInfo info;
            
            while (reader->take_next_sample(&data, &info) == RETCODE_OK) {
                if (info.valid_data) {
                    auto start_time = high_resolution_clock::now();
                    
                    // 构建JSON消息
                    Json::Value json;
                    json["sequence_id"] = data.sequence_id();
                    json["a1_timestamp"] = data.timestamp();
                    json["ebrake_status"] = data.ebrake_status();
                    json["ebrake_status_enable"] = data.ebrake_status_enable();
                    json["gear_position"] = data.gear_position();
                    json["gear_position_enable"] = data.gear_position_enable();
                    json["indicator_left"] = data.indicator_left();
                    json["indicator_left_enable"] = data.indicator_left_enable();
                    json["indicator_right"] = data.indicator_right();
                    json["indicator_right_enable"] = data.indicator_right_enable();
                    json["remote_drive_ready"] = data.remote_drive_ready();
                    json["remote_drive_takeover_request"] = data.remote_drive_takeover_request();
                    json["steering_angle"] = data.steering_angle();
                    json["steering_angle_enable"] = data.steering_angle_enable();
                    json["target_acceleration"] = data.target_acceleration();
                    json["target_acceleration_enable"] = data.target_acceleration_enable();
                    
                    // 转换为字符串并发送
                    Json::FastWriter writer;
                    std::string message = writer.write(json);
                    mosquitto_publish(subscriber_->mosq_, nullptr, 
                        subscriber_->mqtt_topic_,
                        message.length(), message.c_str(),
                        0, false);
                    
                    // 立即刷新缓存，强制写入网络
                    mosquitto_loop_write(subscriber_->mosq_, 0);
                    
                    // 获取a2时间戳（MQTT转发时间）
                    int64_t a2_timestamp = duration_cast<milliseconds>(
                        system_clock::now().time_since_epoch()).count();
                    
                    // 构造日志行
                    std::stringstream log_line;
                    log_line << data.sequence_id() << ","
                            << json["a1_timestamp"].asInt64() << ","
                            << a2_timestamp;
                    
                    // 异步写入日志
                    {
                        std::lock_guard<std::mutex> lock(subscriber_->log_mutex_);
                        subscriber_->log_queue_.push(log_line.str());
                    }
                    subscriber_->log_cv_.notify_one();

                }
            }
        }
     } listener_;
 
 public:
         CloudToVehicleSubscriber() 
        : participant_(nullptr), subscriber_(nullptr), topic_(nullptr), reader_(nullptr),
          type_(new CloudToVehiclePubSubType()), listener_(this), log_running_(true)
    {
        if (!init()) {
            throw std::runtime_error("初始化失败");
        }
        // 启动日志线程
        log_thread_ = std::thread(&CloudToVehicleSubscriber::log_writer_thread, this);
    }
 
         ~CloudToVehicleSubscriber() {
        // 停止日志线程
        log_running_ = false;
        log_cv_.notify_one();
        if (log_thread_.joinable()) {
            log_thread_.join();
        }

        // 清理FastDDS资源
        if (reader_ != nullptr) {
            subscriber_->delete_datareader(reader_);
        }
        if (subscriber_ != nullptr) {
            participant_->delete_subscriber(subscriber_);
        }
        if (topic_ != nullptr) {
            participant_->delete_topic(topic_);
        }
        if (participant_ != nullptr) {
            DomainParticipantFactory::get_instance()->delete_participant(participant_);
        }
        
        // 清理MQTT资源
        if (mosq_) {
            mosquitto_disconnect(mosq_);
            mosquitto_destroy(mosq_);
        }
        mosquitto_lib_cleanup();
        
        // 关闭日志文件
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }
 
         void log_writer_thread() {
        std::vector<std::string> buffer;
        const size_t BUFFER_SIZE = 100;  // 缓冲100条数据再写入
        
        while (log_running_) {
            std::string log_line;
            {
                std::unique_lock<std::mutex> lock(log_mutex_);
                if (log_queue_.empty()) {
                    // 等待新的日志数据或停止信号
                    log_cv_.wait_for(lock, std::chrono::seconds(1));
                    continue;
                }
                log_line = log_queue_.front();
                log_queue_.pop();
            }
            
            buffer.push_back(log_line);
            
            // 当缓冲区满或队列为空时写入文件
            if (buffer.size() >= BUFFER_SIZE || log_queue_.empty()) {
                for (const auto& line : buffer) {
                    log_file_ << line << std::endl;
                }
                log_file_.flush();
                buffer.clear();
            }
        }
        
        // 程序结束时，写入剩余的日志
        if (!buffer.empty()) {
            for (const auto& line : buffer) {
                log_file_ << line << std::endl;
            }
            log_file_.flush();
        }
    }

    bool init() {
        // 初始化日志
        setup_logging();
         
         // 初始化MQTT
         init_mqtt();
         
         // 初始化FastDDS
         return init_fastdds();
     }
 
 private:
         void setup_logging() {
        // 创建基础日志目录
        std::string base_log_dir = "../logs";
        std::string cmd = "mkdir -p " + base_log_dir;
        if (system(cmd.c_str()) != 0) {
            throw std::runtime_error("Failed to create base log directory");
        }

        // 获取当前时间
        auto now = system_clock::now();
        auto now_time = system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&now_time), "%Y%m%d_%H%M%S");
        
        // 创建具体的日志目录
        log_dir_ = base_log_dir + "/bridge_" + ss.str();
        cmd = "mkdir -p " + log_dir_;
        if (system(cmd.c_str()) != 0) {
            throw std::runtime_error("Failed to create log directory");
        }
        
        // 打开日志文件
        std::string log_file = log_dir_ + "/bridge_data.csv";
        log_file_.open(log_file);
        if (!log_file_.is_open()) {
            throw std::runtime_error("Failed to open log file: " + log_file);
        }
        
        // 写入标题行
        log_file_ << "sequence_id,a1_timestamp,a2_timestamp\n";
        log_file_.flush();
    }
     
         void init_mqtt() {
        mosquitto_lib_init();
        mosq_ = mosquitto_new(client_id_, true, this);
        if (!mosq_) {
            throw std::runtime_error("Failed to create MQTT client");
        }
        
        int rc = mosquitto_connect(mosq_, mqtt_host_, mqtt_port_, 60);
        if (rc != MOSQ_ERR_SUCCESS) {
            throw std::runtime_error("Failed to connect to MQTT broker: " + std::string(mosquitto_strerror(rc)));
        }
        
        rc = mosquitto_loop_start(mosq_);
        if (rc != MOSQ_ERR_SUCCESS) {
            throw std::runtime_error("Failed to start MQTT loop: " + std::string(mosquitto_strerror(rc)));
        }
    }
     
         bool init_fastdds() {
        // 创建参与者
        DomainParticipantQos pqos;
        pqos.name("CloudToVehicle_Subscriber");
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

        // 创建订阅者
        subscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
        if (subscriber_ == nullptr) {
            return false;
        }

        // 创建数据读取器
        DataReaderQos rqos;
        rqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
        reader_ = subscriber_->create_datareader(
            topic_,
            rqos,
            &listener_);
        if (reader_ == nullptr) {
            return false;
        }

        return true;
    }
 };
 
 int main() {
    try {
        CloudToVehicleSubscriber subscriber;
        while (true) {
            std::this_thread::sleep_for(seconds(1));
        }
    } catch (const std::exception& e) {
        return 1;
    }
    return 0;
}