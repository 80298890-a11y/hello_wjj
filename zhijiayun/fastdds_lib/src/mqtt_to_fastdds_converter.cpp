/**
 * @file mqtt_to_fastdds_converter.cpp
 * @brief MQTT到FastDDS转换器通用库实现
 */

#include "mqtt_to_fastdds_converter.hpp"
#include <iostream>
#include <chrono>
#include <sstream>
#include <thread>

// 预定义的MQTT主题列表
const std::vector<std::string> MQTTToFastDDSConverter::DEFAULT_MQTT_TOPICS = {
    "/handshake/request",
    "/handshake/response", 
    "/vehicle/vehicle_status",
    "/vehicle/control_cmd"
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
        std::cerr << "❌ FastDDS发布者初始化失败" << std::endl;
        return false;
    }
    
    // 2. 初始化MQTT客户端
    if (!initMQTT()) {
        std::cerr << "❌ MQTT客户端初始化失败" << std::endl;
        return false;
    }
    
    // 3. 订阅所有MQTT主题
    if (!subscribeAllMQTTTopics()) {
        std::cerr << "❌ MQTT主题订阅失败" << std::endl;
        return false;
    }
    
    std::cout << "✅ MQTT到FastDDS转换器初始化成功" << std::endl;
    return true;
}

void MQTTToFastDDSConverter::cleanup() {
    std::cout << "清理MQTT到FastDDS转换器..." << std::endl;
    
    cleanupMQTT();
    FastDDSPublisher::cleanup();
    
    std::cout << "✅ MQTT到FastDDS转换器清理完成" << std::endl;
}

// =========================== MQTT初始化和清理 ===========================

bool MQTTToFastDDSConverter::initMQTT() {
    std::cout << "初始化MQTT客户端..." << std::endl;
    
    // 初始化mosquitto库
    mosquitto_lib_init();
    
    // 创建MQTT客户端实例
    mqtt_client_ = mosquitto_new(client_id_.c_str(), true, this);
    if (!mqtt_client_) {
        std::cerr << "❌ 创建MQTT客户端失败" << std::endl;
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
        std::cerr << "❌ 连接MQTT服务器失败: " << mosquitto_strerror(result) << std::endl;
        return false;
    }
    
    // 启动MQTT网络处理循环
    result = mosquitto_loop_start(mqtt_client_);
    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "❌ 启动MQTT循环失败: " << mosquitto_strerror(result) << std::endl;
        return false;
    }
    
    // 等待连接建立
    for (int i = 0; i < 100 && !mqtt_connected_; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    if (!mqtt_connected_) {
        std::cerr << "❌ MQTT连接超时" << std::endl;
        return false;
    }
    
    std::cout << "✅ MQTT客户端初始化成功，连接到 " << mqtt_host_ << ":" << mqtt_port_ << std::endl;
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
        std::cerr << "❌ MQTT客户端未初始化" << std::endl;
        return false;
    }
    
    int result = mosquitto_subscribe(mqtt_client_, nullptr, topic.c_str(), qos);
    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "❌ 订阅MQTT主题失败 [" << topic << "]: " << mosquitto_strerror(result) << std::endl;
        return false;
    }
    
    std::cout << "✅ 订阅MQTT主题: " << topic << std::endl;
    return true;
}

bool MQTTToFastDDSConverter::subscribeAllMQTTTopics() {
    std::cout << "订阅所有MQTT主题..." << std::endl;
    
    // 重置订阅确认计数器
    subscriptions_confirmed_ = 0;
    
    bool success = true;
    for (const auto& topic : DEFAULT_MQTT_TOPICS) {
        success &= subscribeMQTTTopic(topic);
    }
    
    if (!success) {
        std::cerr << "❌ 部分MQTT主题订阅失败" << std::endl;
        return false;
    }
    
    // 等待所有订阅确认
    std::cout << "等待所有MQTT订阅确认..." << std::endl;
    int expected_confirmations = DEFAULT_MQTT_TOPICS.size();
    for (int i = 0; i < 100 && subscriptions_confirmed_ < expected_confirmations; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    if (subscriptions_confirmed_ >= expected_confirmations) {
        std::cout << "✅ 所有MQTT主题订阅确认完成 (" << subscriptions_confirmed_ << "/" << expected_confirmations << ")" << std::endl;
    } else {
        std::cerr << "❌ MQTT订阅确认超时 (" << subscriptions_confirmed_ << "/" << expected_confirmations << ")" << std::endl;
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
        std::cout << "✅ MQTT连接成功" << std::endl;
        converter->mqtt_connected_ = true;
    } else {
        std::cerr << "❌ MQTT连接失败: " << mosquitto_connack_string(result) << std::endl;
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
        
        std::cout << "📥 接收到MQTT消息 [" << topic << "]: " << payload << std::endl;
        
        // 处理并转换消息
        bool convert_success = converter->processAndConvert(topic, payload);
        if (convert_success) {
            std::cout << "✅ 消息转换成功" << std::endl;
            // 添加微小延迟避免FastDDS发布冲突
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        } else {
            std::cout << "❌ 消息转换失败" << std::endl;
        }
        
        // 调用用户回调函数
        if (converter->message_callback_) {
            converter->message_callback_(topic, payload);
        }
    }
}

void MQTTToFastDDSConverter::onMQTTSubscribe(struct mosquitto* mosq, void* obj, int mid, int qos_count, const int* granted_qos) {
    MQTTToFastDDSConverter* converter = static_cast<MQTTToFastDDSConverter*>(obj);
    converter->subscriptions_confirmed_++;
    std::cout << "✅ MQTT订阅确认 (mid: " << mid << ", 已确认: " << converter->subscriptions_confirmed_ << ")" << std::endl;
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
        std::cerr << "❌ JSON解析失败: " << errors << std::endl;
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
    } else if (topic == "/handshake/response") {
        return convertHandshakeResponse(json);
    } else if (topic == "/vehicle/vehicle_status") {
        return convertVehicleStatus(json);
    } else if (topic == "/vehicle/control_cmd") {
        return convertRemoteControl(json);
    } else {
        std::cerr << "❌ 未知的MQTT主题: " << topic << std::endl;
        return false;
    }
}

// =========================== 特定消息转换实现 ===========================

bool MQTTToFastDDSConverter::convertHandshakeRequest(const Json::Value& json) {
    long noa_active = getJSONLong(json, "noa_active", 0);
    long override_status = getJSONLong(json, "override_status", 0);
    long override_ready = getJSONLong(json, "override_ready", 0);
    uint64_t original_timestamp = getJSONUInt64(json, "mqtt_send_timestamp", 0);
    
    // 使用原始时间戳，如果没有则使用当前时间
    uint64_t timestamp = original_timestamp > 0 ? original_timestamp : getCurrentTimestamp();
    
    bool result = publishHandshakeRequest("/handshake/request", noa_active, override_status, override_ready, timestamp);
    
    if (result) {
        std::cout << "✅ HandshakeRequest转换成功: noa_active=" << noa_active 
                  << ", override_status=" << override_status 
                  << ", override_ready=" << override_ready 
                  << ", timestamp=" << timestamp << std::endl;
    } else {
        std::cerr << "❌ HandshakeRequest转换失败" << std::endl;
    }
    
    return result;
}

bool MQTTToFastDDSConverter::convertHandshakeResponse(const Json::Value& json) {
    long noa_active = getJSONLong(json, "noa_active", 0);
    long override_response = getJSONLong(json, "override_response", 0);
    bool control_source = getJSONBool(json, "control_source", false);
    uint64_t original_timestamp = getJSONUInt64(json, "mqtt_send_timestamp", 0);
    
    uint64_t timestamp = original_timestamp > 0 ? original_timestamp : getCurrentTimestamp();
    
    bool result = publishHandshakeResponse("/handshake/response", noa_active, override_response, control_source, timestamp);
    
    if (result) {
        std::cout << "✅ HandshakeResponse转换成功: noa_active=" << noa_active 
                  << ", override_response=" << override_response 
                  << ", control_source=" << (control_source ? "true" : "false")
                  << ", timestamp=" << timestamp << std::endl;
    } else {
        std::cerr << "❌ HandshakeResponse转换失败" << std::endl;
    }
    
    return result;
}

bool MQTTToFastDDSConverter::convertVehicleStatus(const Json::Value& json) {
    uint32_t vehicle_id = getJSONUInt32(json, "vehicle_id", 1);
    float speed = getJSONFloat(json, "speed", 0.0f);
    int control_mode = static_cast<int>(getJSONLong(json, "control_mode", 0));
    float longitude = getJSONFloat(json, "position_longitude", 0.0f);
    float latitude = getJSONFloat(json, "position_latitude", 0.0f);
    
    bool result = publishVehicleStatus("/vehicle/vehicle_status", vehicle_id, speed, control_mode, longitude, latitude);
    
    if (result) {
        std::cout << "✅ VehicleStatus转换成功: vehicle_id=" << vehicle_id 
                  << ", speed=" << speed 
                  << ", control_mode=" << control_mode 
                  << ", longitude=" << longitude 
                  << ", latitude=" << latitude << std::endl;
    } else {
        std::cerr << "❌ VehicleStatus转换失败" << std::endl;
    }
    
    return result;
}

bool MQTTToFastDDSConverter::convertRemoteControl(const Json::Value& json) {
    bool steering_enable = getJSONBool(json, "steering_angle_enable", false);
    float steering_angle = getJSONFloat(json, "steering_angle", 0.0f);
    bool accel_enable = getJSONBool(json, "target_acceleration_enable", false);
    float acceleration = getJSONFloat(json, "target_acceleration", 0.0f);
    
    bool result = publishRemoteControl("/vehicle/control_cmd", steering_enable, steering_angle, accel_enable, acceleration);
    
    if (result) {
        std::cout << "✅ RemoteControl转换成功: steering_enable=" << (steering_enable ? "true" : "false")
                  << ", steering_angle=" << steering_angle 
                  << ", accel_enable=" << (accel_enable ? "true" : "false")
                  << ", acceleration=" << acceleration << std::endl;
    } else {
        std::cerr << "❌ RemoteControl转换失败" << std::endl;
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
