#include "mqtt_mosquitto.hpp"
#include <cstring>
#include <chrono>
#include <thread>

MQTTMosquitto::MQTTMosquitto(const std::string& host, int port, const std::string& client_id)
    : broker_host_(host), broker_port_(port), client_id_(client_id), 
      connected_(false), running_(true), mosq_(nullptr)
{
    // 初始化mosquitto库
    mosquitto_lib_init();
    
    // 创建mosquitto客户端实例 (client_id, clean_session=true, userdata=this)
    mosq_ = mosquitto_new(client_id.c_str(), true, this);
    if (!mosq_) {
        std::cerr << "错误: 无法创建MQTT客户端" << std::endl;
        return;
    }
    
    // 设置回调函数
    mosquitto_connect_callback_set(mosq_, on_connect);
    mosquitto_disconnect_callback_set(mosq_, on_disconnect);
    mosquitto_publish_callback_set(mosq_, on_publish);
    
    // 启动工作线程
    worker_thread_ = std::thread(&MQTTMosquitto::worker_loop, this);
}

MQTTMosquitto::~MQTTMosquitto() {
    // 停止工作线程
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        running_ = false;
    }
    queue_cv_.notify_one();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    // 清理MQTT资源
    if (mosq_) {
        mosquitto_disconnect(mosq_);
        mosquitto_destroy(mosq_);
    }
    mosquitto_lib_cleanup();
}

bool MQTTMosquitto::connect() {
    if (!mosq_) {
        std::cerr << "MQTT客户端未初始化" << std::endl;
        return false;
    }
    
    int rc = mosquitto_connect(mosq_, broker_host_.c_str(), broker_port_, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "错误: 无法连接到MQTT broker " << broker_host_ << ":" << broker_port_ 
                  << " - " << mosquitto_strerror(rc) << std::endl;
        return false;
    }
    
    // 启动mosquitto网络循环
    rc = mosquitto_loop_start(mosq_);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "错误: 无法启动MQTT网络循环 - " << mosquitto_strerror(rc) << std::endl;
        return false;
    }
    
    // 等待连接建立
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    return connected_;
}

void MQTTMosquitto::disconnect() {
    if (mosq_ && connected_) {
        mosquitto_loop_stop(mosq_, false);
        mosquitto_disconnect(mosq_);
        connected_ = false;
    }
}

void MQTTMosquitto::publish_async(const std::string& topic, const std::string& payload) {
    if (!connected_) {
        std::cerr << "❌ MQTT未连接，无法发送消息到主题: " << topic << std::endl;
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (running_) {
            message_queue_.push({topic, payload});
        }
    }
    queue_cv_.notify_one();
}

void MQTTMosquitto::worker_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // 等待消息或停止信号
        queue_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
            return !message_queue_.empty() || !running_;
        });
        
        // 处理消息队列
        while (!message_queue_.empty() && connected_) {
            auto message = message_queue_.front();
            message_queue_.pop();
            lock.unlock();
            
            // 发送MQTT消息
            send_mqtt_message(message.first, message.second);
            
            lock.lock();
        }
    }
}

bool MQTTMosquitto::send_mqtt_message(const std::string& topic, const std::string& payload) {
    if (!mosq_ || !connected_) {
        return false;
    }
    
    int mid;
    int rc = mosquitto_publish(mosq_, &mid, topic.c_str(), 
                              payload.length(), payload.c_str(), 
                              0, false);  // QoS 0, not retained
    
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "❌ MQTT消息发送失败: " << mosquitto_strerror(rc) << std::endl;
        return false;
    }
    
    std::cout << "✅ MQTT消息已发送: 主题=" << topic << ", 大小=" << payload.length() << "字节" << std::endl;
    return true;
}

// 静态回调函数实现
void MQTTMosquitto::on_connect(struct mosquitto *mosq, void *userdata, int result) {
    MQTTMosquitto* bridge = static_cast<MQTTMosquitto*>(userdata);
    
    if (result == 0) {
        bridge->connected_ = true;
        std::cout << "✅ 成功连接到MQTT broker: " << bridge->broker_host_ << ":" << bridge->broker_port_ << std::endl;
    } else {
        std::cerr << "❌ MQTT连接失败，错误码: " << result << std::endl;
    }
}

void MQTTMosquitto::on_disconnect(struct mosquitto *mosq, void *userdata, int result) {
    MQTTMosquitto* bridge = static_cast<MQTTMosquitto*>(userdata);
    bridge->connected_ = false;
    
    if (result != 0) {
        std::cerr << "⚠️ MQTT意外断开连接" << std::endl;
    }
}

void MQTTMosquitto::on_publish(struct mosquitto *mosq, void *userdata, int mid) {
    // 消息发布确认 - 可以在这里添加确认逻辑
}
