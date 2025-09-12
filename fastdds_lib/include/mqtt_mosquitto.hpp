#ifndef MQTT_MOSQUITTO_HPP
#define MQTT_MOSQUITTO_HPP

#include <mosquitto.h>
#include <string>
#include <memory>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

// 基于libmosquitto的MQTT桥接类
class MQTTMosquitto {
private:
    struct mosquitto* mosq_;
    std::string broker_host_;
    int broker_port_;
    std::string client_id_;
    bool connected_;
    
    // 异步发送队列
    std::queue<std::pair<std::string, std::string>> message_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_thread_;
    bool running_;

public:
    MQTTMosquitto(const std::string& host, int port, const std::string& client_id = "fastdds_bridge");
    ~MQTTMosquitto();
    
    bool connect();
    void disconnect();
    void publish_async(const std::string& topic, const std::string& payload);
    bool is_connected() const { return connected_; }
    
private:
    void worker_loop();
    bool send_mqtt_message(const std::string& topic, const std::string& payload);
    
    // MQTT回调函数
    static void on_connect(struct mosquitto *mosq, void *userdata, int result);
    static void on_disconnect(struct mosquitto *mosq, void *userdata, int result);
    static void on_publish(struct mosquitto *mosq, void *userdata, int mid);
};

#endif // MQTT_MOSQUITTO_HPP
