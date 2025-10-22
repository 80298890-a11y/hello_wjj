/**
 * @file mqtt_to_fastdds_converter.cpp
 * @brief MQTT到FastDDS转换器通用库实现
 */

#include "mqtt_to_fastdds_converter.hpp"
#include <iostream>
#include <chrono>
#include <sstream>
#include <thread>

// 预定义的MQTT主题列表 - 只转发云端→车端的消息
const std::vector<std::string> MQTTToFastDDSConverter::DEFAULT_MQTT_TOPICS = {
    "/handshake/request",      // 云端握手请求→车端
    "/vehicle/control_cmd"     // 云端控制命令→车端
};

// =========================== 构造函数和析构函数 ===========================

MQTTToFastDDSConverter::MQTTToFastDDSConverter(
    int domain_id,
    const std::string& mqtt_host,
    int mqtt_port,
    const std::string& client_id)
    : FastDDSPublisher(domain_id)
    , mqtt_client_(nullptr)
    , mqtt_host_(mqtt_host)
    , mqtt_port_(mqtt_port)
    , client_id_(client_id) {
}

MQTTToFastDDSConverter::~MQTTToFastDDSConverter() {
    cleanup();
}

// =========================== 初始化和清理 ===========================

bool MQTTToFastDDSConverter::init() {
    std::cout << "初始化MQTT到FastDDS转换器..." << std::endl;
    
    // 1. 初始化FastDDS发布者
    if (!FastDDSPublisher::init()) {
        std::cerr << "[ERROR] FastDDS发布者初始化失败" << std::endl;
        return false;
    }
    
    // 2. 初始化MQTT客户端
    if (!initMQTT()) {
        std::cerr << "[ERROR] MQTT客户端初始化失败" << std::endl;
        return false;
    }
    
    // 3. 订阅所有MQTT主题
    if (!subscribeAllMQTTTopics()) {
        std::cerr << "[ERROR] MQTT主题订阅失败" << std::endl;
        return false;
    }
    
    std::cout << "[SUCCESS] MQTT到FastDDS转换器初始化成功" << std::endl;
    return true;
}

void MQTTToFastDDSConverter::cleanup() {
    std::cout << "清理MQTT到FastDDS转换器..." << std::endl;
    
    cleanupMQTT();
    FastDDSPublisher::cleanup();
    
    std::cout << "[CLEANUP] MQTT到FastDDS转换器清理完成" << std::endl;
}

// =========================== MQTT初始化和清理 ===========================

bool MQTTToFastDDSConverter::initMQTT() {
    std::cout << "初始化MQTT客户端..." << std::endl;
    
    // 初始化mosquitto库
    mosquitto_lib_init();
    
    // 创建MQTT客户端实例
    mqtt_client_ = mosquitto_new(client_id_.c_str(), true, this);
    if (!mqtt_client_) {
        std::cerr << "[ERROR] 创建MQTT客户端失败" << std::endl;
        return false;
    }
    
    // 设置回调函数
    mosquitto_connect_callback_set(mqtt_client_, onMQTTConnect);
    mosquitto_disconnect_callback_set(mqtt_client_, onMQTTDisconnect);
    mosquitto_message_callback_set(mqtt_client_, onMQTTMessage);
    mosquitto_subscribe_callback_set(mqtt_client_, onMQTTSubscribe);
    
    // 连接到MQTT服务器
    int result = mosquitto_connect(mqtt_client_, mqtt_host_.c_str(), mqtt_port_, 60);
    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "[ERROR] 连接MQTT服务器失败: " << mosquitto_strerror(result) << std::endl;
        return false;
    }
    
    // 启动MQTT网络处理循环
    result = mosquitto_loop_start(mqtt_client_);
    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "[ERROR] 启动MQTT循环失败: " << mosquitto_strerror(result) << std::endl;
        return false;
    }
    
    // 等待连接建立
    for (int i = 0; i < 100 && !mqtt_connected_; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    if (!mqtt_connected_) {
        std::cerr << "[ERROR] MQTT连接超时" << std::endl;
        return false;
    }
    
    std::cout << "[SUCCESS] MQTT客户端初始化成功，连接到 " << mqtt_host_ << ":" << mqtt_port_ << std::endl;
    return true;
}

void MQTTToFastDDSConverter::cleanupMQTT() {
    if (mqtt_client_) {
        std::cout << "停止MQTT网络循环..." << std::endl;
        mosquitto_loop_stop(mqtt_client_, true);
        
        std::cout << "断开MQTT连接..." << std::endl;
        mosquitto_disconnect(mqtt_client_);
        
        std::cout << "销毁MQTT客户端..." << std::endl;
        mosquitto_destroy(mqtt_client_);
        mqtt_client_ = nullptr;
    }
    
    std::cout << "清理MQTT库..." << std::endl;
    mosquitto_lib_cleanup();
}

// =========================== MQTT订阅功能 ===========================

bool MQTTToFastDDSConverter::subscribeMQTTTopic(const std::string& topic, int qos) {
    if (!mqtt_client_) {
        std::cerr << "[ERROR] MQTT客户端未初始化" << std::endl;
        return false;
    }
    
    std::cout << "[PROC] 尝试订阅MQTT主题: " << topic << " (QoS: " << qos << ")" << std::endl;
    
    int result = mosquitto_subscribe(mqtt_client_, nullptr, topic.c_str(), qos);
    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "[ERROR] 订阅MQTT主题失败 [" << topic << "]: " << mosquitto_strerror(result) << " (错误代码: " << result << ")" << std::endl;
        return false;
    }
    
    std::cout << "[SUCCESS] 订阅MQTT主题: " << topic << std::endl;
    return true;
}

bool MQTTToFastDDSConverter::subscribeAllMQTTTopics() {
    std::cout << "订阅所有MQTT主题..." << std::endl;
    std::cout << "预定义主题列表大小: " << DEFAULT_MQTT_TOPICS.size() << std::endl;
    
    // 重置订阅确认计数器
    subscriptions_confirmed_ = 0;
    
    bool success = true;
    for (const auto& topic : DEFAULT_MQTT_TOPICS) {
        std::cout << "[PROC] 处理主题: '" << topic << "'" << std::endl;
        success &= subscribeMQTTTopic(topic);
    }
    
    if (!success) {
        std::cerr << "[ERROR] 部分MQTT主题订阅失败" << std::endl;
        return false;
    }
    
    // 等待所有订阅确认
    std::cout << "等待所有MQTT订阅确认..." << std::endl;
    int expected_confirmations = DEFAULT_MQTT_TOPICS.size();
    for (int i = 0; i < 100 && subscriptions_confirmed_ < expected_confirmations; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    if (subscriptions_confirmed_ >= expected_confirmations) {
        std::cout << "[SUCCESS] 所有MQTT主题订阅确认完成 (" << subscriptions_confirmed_ << "/" << expected_confirmations << ")" << std::endl;
    } else {
        std::cerr << "[ERROR] MQTT订阅确认超时 (" << subscriptions_confirmed_ << "/" << expected_confirmations << ")" << std::endl;
        return false;
    }
    
    return true;
}

void MQTTToFastDDSConverter::setMQTTMessageCallback(MQTTMessageCallback callback) {
    message_callback_ = callback;
}

// =========================== MQTT回调函数 ===========================

void MQTTToFastDDSConverter::onMQTTConnect(struct mosquitto* mosq, void* obj, int result) {
    MQTTToFastDDSConverter* converter = static_cast<MQTTToFastDDSConverter*>(obj);
    
    if (result == 0) {
        std::cout << "[SUCCESS] MQTT连接成功" << std::endl;
        converter->mqtt_connected_ = true;
    } else {
        std::cerr << "[ERROR] MQTT连接失败: " << mosquitto_connack_string(result) << std::endl;
        converter->mqtt_connected_ = false;
    }
}

void MQTTToFastDDSConverter::onMQTTDisconnect(struct mosquitto* mosq, void* obj, int result) {
    MQTTToFastDDSConverter* converter = static_cast<MQTTToFastDDSConverter*>(obj);
    
    std::cout << "MQTT连接断开: " << mosquitto_strerror(result) << std::endl;
    converter->mqtt_connected_ = false;
}

void MQTTToFastDDSConverter::onMQTTMessage(struct mosquitto* mosq, void* obj, const struct mosquitto_message* message) {
    MQTTToFastDDSConverter* converter = static_cast<MQTTToFastDDSConverter*>(obj);
    
    if (message->payloadlen > 0) {
        std::string topic(message->topic);
        std::string payload(static_cast<char*>(message->payload), message->payloadlen);
        
        // 底层不再打印每条消息，由上层智能打印逻辑负责
        // 获取当前时间戳用于详细日志
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        
        // 处理并转换消息
        bool convert_success = converter->processAndConvert(topic, payload);
        
        if (convert_success) {
            // 添加微小延迟避免FastDDS发布冲突
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        // 成功/失败信息由上层智能打印逻辑处理
        
        // 调用用户回调函数，传递转换结果
        if (converter->message_callback_) {
            converter->message_callback_(topic, payload, convert_success);
        }
    }
}

void MQTTToFastDDSConverter::onMQTTSubscribe(struct mosquitto* mosq, void* obj, int mid, int qos_count, const int* granted_qos) {
    MQTTToFastDDSConverter* converter = static_cast<MQTTToFastDDSConverter*>(obj);
    converter->subscriptions_confirmed_++;
    
    std::cout << "[SUCCESS] MQTT订阅确认 (mid: " << mid << ", QoS计数: " << qos_count;
    if (qos_count > 0 && granted_qos) {
        std::cout << ", 授予QoS: [";
        for (int i = 0; i < qos_count; i++) {
            if (i > 0) std::cout << ", ";
            std::cout << granted_qos[i];
        }
        std::cout << "]";
    }
    std::cout << ", 已确认: " << converter->subscriptions_confirmed_ << ")" << std::endl;
}

// =========================== JSON解析辅助函数 ===========================

bool MQTTToFastDDSConverter::parseJSON(const std::string& json_str, Json::Value& json_value) {
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();
    std::string errors;
    
    bool success = reader->parse(
        json_str.c_str(),
        json_str.c_str() + json_str.length(),
        &json_value,
        &errors
    );
    
    delete reader;
    
    if (!success) {
        std::cerr << "[ERROR] JSON解析失败: " << errors << std::endl;
    }
    
    return success;
}

long MQTTToFastDDSConverter::getJSONLong(const Json::Value& json, const std::string& key, long default_value) {
    if (json.isMember(key) && json[key].isNumeric()) {
        return json[key].asInt64();
    }
    return default_value;
}

float MQTTToFastDDSConverter::getJSONFloat(const Json::Value& json, const std::string& key, float default_value) {
    if (json.isMember(key) && json[key].isNumeric()) {
        return json[key].asFloat();
    }
    return default_value;
}

bool MQTTToFastDDSConverter::getJSONBool(const Json::Value& json, const std::string& key, bool default_value) {
    if (json.isMember(key)) {
        if (json[key].isBool()) {
            return json[key].asBool();
        } else if (json[key].isNumeric()) {
            return json[key].asInt() != 0;
        }
    }
    return default_value;
}

uint32_t MQTTToFastDDSConverter::getJSONUInt32(const Json::Value& json, const std::string& key, uint32_t default_value) {
    if (json.isMember(key) && json[key].isNumeric()) {
        return json[key].asUInt();
    }
    return default_value;
}

uint64_t MQTTToFastDDSConverter::getJSONUInt64(const Json::Value& json, const std::string& key, uint64_t default_value) {
    if (json.isMember(key) && json[key].isNumeric()) {
        return json[key].asUInt64();
    }
    return default_value;
}

// =========================== 消息处理和转换 ===========================

bool MQTTToFastDDSConverter::processAndConvert(const std::string& topic, const std::string& json_message) {
    // 解析JSON
    Json::Value json;
    if (!parseJSON(json_message, json)) {
        return false;
    }
    
    // 根据topic类型进行转换
    if (topic == "/handshake/request") {
        return convertHandshakeRequest(json);
    } else if (topic == "/vehicle/control_cmd") {
        return convertRemoteControl(json);

    }else {
        std::cerr << "[ERROR] 未知的MQTT主题: " << topic << std::endl;
        return false;
    }
}

// =========================== 特定消息转换实现 ===========================

bool MQTTToFastDDSConverter::convertHandshakeRequest(const Json::Value& json) {
    // 根据Handshake.idl的完整字段定义解析
    long noa_active_request = getJSONLong(json, "noa_active_request", 1);
    long remote_override_status = getJSONLong(json, "remote_override_status", 0);
    long remote_override_ready = getJSONLong(json, "remote_override_ready", 1);
    uint64_t timestamp = getJSONUInt64(json, "timestamp", 0);
    
    // 如果没有timestamp，使用当前时间
    if (timestamp == 0) {
        timestamp = getCurrentTimestamp();
    }
    
    // 延迟分析已移至 mqtt_to_fastdds_bridge.cpp 的回调函数中处理
    // 在上层根据值变化判断是否打印，避免重复打印
    
    bool result = publishHandshakeRequest("/handshake/request", noa_active_request, remote_override_status, remote_override_ready, timestamp);
    
    if (result) {
        // 发布成功信息由上层智能打印逻辑处理
    } else {
        std::cerr << "[ERROR] [HandshakeRequest] FastDDS发布失败: topic='/handshake/request'" << std::endl;
    }
    
    return result;
}


bool MQTTToFastDDSConverter::convertRemoteControl(const Json::Value& json) {
    // 根据RemoteControl.idl的完整14个字段定义解析
    bool steering_angle_enable = getJSONBool(json, "steering_angle_enable", 0);
    float steering_angle = getJSONFloat(json, "steering_angle", 15.5f);
    bool target_acceleration_enable = getJSONBool(json, "target_acceleration_enable", 0);
    float target_acceleration = getJSONFloat(json, "target_acceleration", 0.0f);
    bool indicator_left_enable = getJSONBool(json, "indicator_left_enable", 0);
    bool indicator_left = getJSONBool(json, "indicator_left", 0);
    bool indicator_right_enable = getJSONBool(json, "indicator_right_enable", 0);
    bool indicator_right = getJSONBool(json, "indicator_right", 0);
    bool gear_position_enable = getJSONBool(json, "gear_position_enable", 1);
    long gear_position = getJSONLong(json, "gear_position", 1);
    bool ebrake_status_enable = getJSONBool(json, "ebrake_status_enable", 1);
    long ebrake_status = getJSONLong(json, "ebrake_status", 0);
    long lane_change = getJSONLong(json, "lane_change", 0);
    uint64_t timestamp = getJSONUInt64(json, "timestamp", 0);
    
    // 如果没有timestamp，使用当前时间
    if (timestamp == 0) {
        timestamp = getCurrentTimestamp();
    }
    
    // 延迟分析已移至 mqtt_to_fastdds_bridge.cpp 的回调函数中处理
    // 在上层根据值变化判断是否打印，避免重复打印
    
    bool result = publishRemoteControl("/vehicle/control_cmd", 
                                      steering_angle_enable, steering_angle,
                                      target_acceleration_enable, target_acceleration,
                                      indicator_left_enable, indicator_left,
                                      indicator_right_enable, indicator_right,
                                      gear_position_enable, gear_position,
                                      ebrake_status_enable, ebrake_status,
                                      lane_change,
                                      timestamp);
    
    if (result) {
        // 发布成功信息由上层智能打印逻辑处理
    } else {
        std::cerr << "[ERROR] [RemoteControl] FastDDS发布失败: topic='/vehicle/control_cmd'" << std::endl;
    }
    
    return result;
}

// =========================== 状态查询 ===========================

bool MQTTToFastDDSConverter::isMQTTConnected() const {
    return mqtt_connected_;
}

std::string MQTTToFastDDSConverter::getMQTTStatusString() const {
    if (mqtt_connected_) {
        return "已连接到 " + mqtt_host_ + ":" + std::to_string(mqtt_port_);
    } else {
        return "未连接";
    }
}


