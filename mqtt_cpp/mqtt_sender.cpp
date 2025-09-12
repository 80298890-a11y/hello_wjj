#include <mosquitto.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <signal.h>
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <queue>
#include <mutex>
#include <condition_variable>

// 全局变量
volatile bool running_ = true;
struct mosquitto* mosq = nullptr;

// 消息队列
std::queue<std::string> mqtt_message_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;

// 信号处理函数
void signal_handler(int signum) {
    std::cout << "\n接收到信号 " << signum << "，正在停止程序..." << std::endl;
    running_ = false;
    queue_cv.notify_all();
}

// MQTT连接回调
void mqtt_connect_callback(struct mosquitto* mosq, void* userdata, int result) {
    // 静默模式，不打印连接信息
}

// MQTT发布回调
void mqtt_publish_callback(struct mosquitto* mosq, void* userdata, int mid) {
    // 静默模式，不打印发布信息
}

// MQTT断开连接回调
void mqtt_disconnect_callback(struct mosquitto* mosq, void* userdata, int result) {
    // 静默模式，不打印断开信息
}

// 获取当前时间戳（毫秒）
int64_t get_current_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
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

// 发送MQTT消息
bool send_mqtt_message(const std::string& message, const std::string& topic = "cloud/vehicle/control") {
    if (!mosq) {
        return false;
    }
    
    int result = mosquitto_publish(mosq, nullptr, topic.c_str(), message.length(), message.c_str(), 1, false);
    
    return (result == MOSQ_ERR_SUCCESS);
}

// MQTT发送线程函数
void mqtt_sender_thread() {
    std::cout << "MQTT发送线程已启动" << std::endl;
    
    while (running_) {
        std::string message;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, []{ return !mqtt_message_queue.empty() || !running_; });
            
            if (!running_) break;
            
            if (!mqtt_message_queue.empty()) {
                message = mqtt_message_queue.front();
                mqtt_message_queue.pop();
            }
        }
        
        if (!message.empty()) {
            send_mqtt_message(message);
        }
    }
    
    std::cout << "MQTT发送线程已停止" << std::endl;
}

// CSV写入线程函数
void csv_writer_thread(std::ofstream& csv_file, std::queue<std::vector<std::string>>& csv_queue, std::mutex& csv_mutex, std::condition_variable& csv_cv) {
    std::vector<std::string> data;
    
    while (running_) {
        {
            std::unique_lock<std::mutex> lock(csv_mutex);
            csv_cv.wait(lock, [&]{ return !csv_queue.empty() || !running_; });
            
            if (!running_) break;
            
            if (!csv_queue.empty()) {
                data = csv_queue.front();
                csv_queue.pop();
            }
        }
        
        if (!data.empty()) {
            // 写入CSV
            for (size_t i = 0; i < data.size(); ++i) {
                if (i > 0) csv_file << ",";
                csv_file << data[i];
            }
            csv_file << "\n";
            csv_file.flush();
        }
    }
}

class MQTTSender {
private:
    std::string broker_;
    int port_;
    std::string topic_;
    int sequence_id_;
    double interval_;
    std::ofstream csv_file_;
    std::queue<std::vector<std::string>> csv_queue_;
    std::mutex csv_mutex_;
    std::condition_variable csv_cv_;
    std::thread csv_thread_;
    
public:
    MQTTSender(const std::string& broker = "hellorobotaxi.cn", int port = 11883, const std::string& topic = "cloud/vehicle/control") 
        : broker_(broker), port_(port), topic_(topic), sequence_id_(0), interval_(0.01) {
        
        // 创建日志目录
        system("mkdir -p logs");
        
        // 创建带时间戳的日志目录
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << "logs/sender_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
        std::string log_dir = ss.str();
        
        // 确保目录唯一
        int counter = 0;
        std::string final_log_dir = log_dir;
        while (system(("test -d " + final_log_dir).c_str()) == 0) {
            counter++;
            final_log_dir = log_dir + "_" + std::to_string(counter);
        }
        
        system(("mkdir -p " + final_log_dir).c_str());
        
        // 创建CSV文件
        csv_file_.open(final_log_dir + "/sender_data.csv");
        csv_file_ << "sequence_id,a1_timestamp\n";
        
        std::cout << "日志目录: " << final_log_dir << std::endl;
        
        // 启动CSV写入线程
        csv_thread_ = std::thread(csv_writer_thread, std::ref(csv_file_), std::ref(csv_queue_), std::ref(csv_mutex_), std::ref(csv_cv_));
    }
    
    ~MQTTSender() {
        running_ = false;
        csv_cv_.notify_all();
        if (csv_thread_.joinable()) {
            csv_thread_.join();
        }
        if (csv_file_.is_open()) {
            csv_file_.close();
        }
    }
    
    void send_message() {
        if (!mosq) {
            return;
        }
        
        try {
            // 获取时间戳并发送消息
            sequence_id_++;
            int64_t a1_timestamp = get_current_timestamp_ms();
            
            // 创建JSON消息
            Json::Value message;
            message["sequence_id"] = sequence_id_;
            message["steering_angle_enable"] = true;
            message["steering_angle"] = 0.0;
            message["target_acceleration_enable"] = true;
            message["target_acceleration"] = 0.0;
            message["indicator_left_enable"] = false;
            message["indicator_left"] = false;
            message["indicator_right_enable"] = false;
            message["indicator_right"] = false;
            message["gear_position_enable"] = false;
            message["gear_position"] = 0;
            message["ebrake_status_enable"] = false;
            message["ebrake_status"] = 0;
            message["remote_drive_ready"] = false;
            message["remote_drive_takeover_request"] = false;
            message["a1_timestamp"] = a1_timestamp;
            
            Json::StreamWriterBuilder writer;
            std::string payload = Json::writeString(writer, message);
            
            // 发送消息
            send_mqtt_message(payload, topic_);
            
            // 记录到CSV队列
            {
                std::lock_guard<std::mutex> lock(csv_mutex_);
                csv_queue_.push({std::to_string(sequence_id_), std::to_string(a1_timestamp)});
            }
            csv_cv_.notify_one();
            
        } catch (const std::exception& e) {
            std::cout << "Error sending message: " << e.what() << std::endl;
        }
    }
    
    void run() {
        try {
            // 初始化MQTT库
            mosquitto_lib_init();
            
            // 创建MQTT客户端
            mosq = mosquitto_new(nullptr, true, nullptr);
            if (!mosq) {
                std::cout << "无法创建MQTT客户端" << std::endl;
                return;
            }
            
            // 设置MQTT回调
            mosquitto_connect_callback_set(mosq, mqtt_connect_callback);
            mosquitto_publish_callback_set(mosq, mqtt_publish_callback);
            mosquitto_disconnect_callback_set(mosq, mqtt_disconnect_callback);
            
            // 连接到MQTT服务器
            int result = mosquitto_connect(mosq, broker_.c_str(), port_, 60);
            if (result != MOSQ_ERR_SUCCESS) {
                std::cout << "MQTT连接失败: " << mosquitto_strerror(result) << std::endl;
                mosquitto_destroy(mosq);
                mosquitto_lib_cleanup();
                return;
            }
            
            // 启动MQTT循环
            mosquitto_loop_start(mosq);
            
            // 启动MQTT发送线程
            std::thread mqtt_thread(mqtt_sender_thread);
            
            std::cout << "MQTT发送程序启动成功" << std::endl;
            std::cout << "发送频率: " << (1.0 / interval_) << "Hz" << std::endl;
            std::cout << "按 Ctrl+C 停止程序" << std::endl;
            
            // 主循环
            while (running_) {
                auto start_time = std::chrono::high_resolution_clock::now();
                send_message();
                auto send_duration = std::chrono::high_resolution_clock::now() - start_time;
                auto sleep_time = std::chrono::duration<double>(interval_) - send_duration;
                if (sleep_time.count() > 0) {
                    std::this_thread::sleep_for(sleep_time);
                }
            }
            
            // 等待MQTT线程结束
            if (mqtt_thread.joinable()) {
                mqtt_thread.join();
            }
            
            // 清理MQTT资源
            mosquitto_loop_stop(mosq, true);
            mosquitto_disconnect(mosq);
            mosquitto_destroy(mosq);
            mosquitto_lib_cleanup();
            
        } catch (const std::exception& e) {
            std::cout << "Error in run loop: " << e.what() << std::endl;
        }
    }
};

int main() {
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    MQTTSender sender;
    sender.run();
    
    std::cout << "程序已停止" << std::endl;
    return 0;
} 