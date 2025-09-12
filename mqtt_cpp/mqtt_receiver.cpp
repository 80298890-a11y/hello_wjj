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

// 回调函数前置声明
void mqtt_connect_callback(struct mosquitto*, void*, int);
void mqtt_message_callback(struct mosquitto*, void*, const struct mosquitto_message*);
void mqtt_disconnect_callback(struct mosquitto*, void*, int);

// 全局变量
volatile bool running_ = true;
struct mosquitto* mosq = nullptr;

// 信号处理函数
void signal_handler(int signum) {
    std::cout << "\n接收到信号 " << signum << "，正在停止程序..." << std::endl;
    running_ = false;
}

// 获取当前时间戳（毫秒）
int64_t get_current_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
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

class MQTTReceiver {
private:
    std::string broker_;
    int port_;
    std::string topic_;
    int b1_sn_;
    std::ofstream csv_file_;
    std::queue<std::vector<std::string>> csv_queue_;
    std::mutex csv_mutex_;
    std::condition_variable csv_cv_;
    std::thread csv_thread_;
    
public:
    MQTTReceiver(const std::string& broker = "hellorobotaxi.cn", int port = 11883, const std::string& topic = "cloud/vehicle/control") 
        : broker_(broker), port_(port), topic_(topic), b1_sn_(0) {
        
        // 创建日志目录
        system("mkdir -p logs");
        
        // 创建带时间戳的日志目录
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << "logs/receiver_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
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
        csv_file_.open(final_log_dir + "/receiver_data.csv");
        csv_file_ << "b1_sn,b1_timestamp\n";
        
        std::cout << "日志目录: " << final_log_dir << std::endl;
        
        // 启动CSV写入线程
        csv_thread_ = std::thread(csv_writer_thread, std::ref(csv_file_), std::ref(csv_queue_), std::ref(csv_mutex_), std::ref(csv_cv_));
    }
    
    ~MQTTReceiver() {
        running_ = false;
        csv_cv_.notify_all();
        if (csv_thread_.joinable()) {
            csv_thread_.join();
        }
        if (csv_file_.is_open()) {
            csv_file_.close();
        }
    }
    
    void process_message(const std::string& payload) {
        try {
            // 解析JSON
            Json::Value root;
            Json::CharReaderBuilder reader;
            std::string errors;
            std::istringstream payload_stream(payload);
            
            if (!Json::parseFromStream(reader, payload_stream, &root, &errors)) {
                std::cout << "JSON解析失败: " << errors << std::endl;
                return;
            }
            
            // 提取sequence_id
            int sequence_id = root.get("sequence_id", 0).asInt();
            
            // 获取接收时间戳
            b1_sn_++;
            int64_t b1_timestamp = get_current_timestamp_ms();
            
            // 记录到CSV队列
            {
                std::lock_guard<std::mutex> lock(csv_mutex_);
                csv_queue_.push({std::to_string(b1_sn_), std::to_string(b1_timestamp)});
            }
            csv_cv_.notify_one();
            
        } catch (const std::exception& e) {
            std::cout << "处理消息时出错: " << e.what() << std::endl;
        }
    }
    
    void run() {
        try {
            // 初始化MQTT库
            mosquitto_lib_init();
            
            // 创建MQTT客户端
            mosq = mosquitto_new(nullptr, true, this);
            if (!mosq) {
                std::cout << "无法创建MQTT客户端" << std::endl;
                return;
            }
            
            // 设置MQTT回调
            mosquitto_connect_callback_set(mosq, mqtt_connect_callback);
            mosquitto_message_callback_set(mosq, mqtt_message_callback);
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
            
            std::cout << "MQTT接收程序启动成功" << std::endl;
            std::cout << "正在监听主题: " << topic_ << std::endl;
            std::cout << "按 Ctrl+C 停止程序" << std::endl;
            
            // 主循环
            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

// MQTT连接回调
void mqtt_connect_callback(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        std::cout << "MQTT连接成功" << std::endl;
        // 订阅主题
        mosquitto_subscribe(mosq, nullptr, "cloud/vehicle/control", 1);
    } else {
        std::cout << "MQTT连接失败，错误码: " << result << std::endl;
    }
}

// MQTT消息回调
void mqtt_message_callback(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    std::string topic = (char*)message->topic;
    std::string payload((char*)message->payload, message->payloadlen);
    
    std::cout << "收到MQTT消息，主题: " << topic << "，长度: " << message->payloadlen << std::endl;
    
    // 将消息传递给主线程处理
    auto* receiver = static_cast<MQTTReceiver*>(userdata);
    if (receiver) {
        receiver->process_message(payload);
    }
}

// MQTT断开连接回调
void mqtt_disconnect_callback(struct mosquitto* mosq, void* userdata, int result) {
    std::cout << "MQTT连接断开，错误码: " << result << std::endl;
}

int main() {
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    MQTTReceiver receiver;
    receiver.run();
    
    std::cout << "程序已停止" << std::endl;
    return 0;
} 