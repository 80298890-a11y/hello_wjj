#include "fastdds_mqtt_publisher.hpp"
#include "mqtt_mosquitto.hpp"

#include <iostream>
#include <chrono>

// 外部声明的详细输出控制变量
extern bool g_verbose_output;

FastDDSMQTTPublisher::FastDDSMQTTPublisher(
    int domain_id,
    const std::string& mqtt_broker,
    int mqtt_port)
    : FastDDSPublisher(domain_id)  // 调用父类构造函数
    , mqtt_broker_(mqtt_broker)
    , mqtt_port_(mqtt_port)
{
}

FastDDSMQTTPublisher::~FastDDSMQTTPublisher()
{
    // 父类析构函数会自动清理FastDDS资源
    // 这里只需要清理MQTT资源
}

bool FastDDSMQTTPublisher::init()
{
    // 1. 初始化FastDDS (调用父类方法)
    if (!FastDDSPublisher::init()) {
        return false;
    }

    // 2. 初始化MQTT连接
    mqtt_bridge_ = std::make_unique<MQTTMosquitto>(mqtt_broker_, mqtt_port_, "fastdds_mqtt_publisher");
    if (!mqtt_bridge_->connect()) {
        std::cerr << "MQTT连接失败" << std::endl;
        return false;
    }

    std::cout << "FastDDS + MQTT发布者初始化成功" << std::endl;
    return true;
}

bool FastDDSMQTTPublisher::publishHandshakeRequest(
    const std::string& topic,
    long noa_active,
    long override_status,
    long override_ready,
    uint64_t timestamp)
{
    // 1. 发布到FastDDS (调用父类方法)
    if (!FastDDSPublisher::publishHandshakeRequest(topic, noa_active, override_status, override_ready, timestamp)) {
        return false;
    }

    // 2. 发布到MQTT
    uint64_t final_timestamp = timestamp == 0 ? getCurrentTimestamp() : timestamp;
    std::string mqtt_payload = createHandshakeRequestMQTTPayload(noa_active, override_status, override_ready, final_timestamp);
    
    if (!publishToMQTT("/handshake/request", mqtt_payload)) {
        std::cerr << "MQTT发布握手请求失败" << std::endl;
        return false;
    }

    if (g_verbose_output) {
        std::cout << "握手请求已发布 (FastDDS + MQTT)" << std::endl;
    }
    return true;
}

bool FastDDSMQTTPublisher::publishHandshakeResponse(
    const std::string& topic,
    long noa_active,
    long override_response,
    bool current_control_source,
    uint64_t timestamp)
{
    // 1. 发布到FastDDS (调用父类方法)
    if (!FastDDSPublisher::publishHandshakeResponse(topic, noa_active, override_response, current_control_source, timestamp)) {
        return false;
    }

    // 2. 发布到MQTT
    uint64_t final_timestamp = timestamp == 0 ? getCurrentTimestamp() : timestamp;
    std::string mqtt_payload = createHandshakeResponseMQTTPayload(noa_active, override_response, current_control_source, final_timestamp);
    
    if (!publishToMQTT("/handshake/response", mqtt_payload)) {
        std::cerr << "MQTT发布握手响应失败" << std::endl;
        return false;
    }

    if (g_verbose_output) {
        std::cout << "握手响应已发布 (FastDDS + MQTT)" << std::endl;
    }
    return true;
}

bool FastDDSMQTTPublisher::publishVehicleStatus(
    const std::string& topic,
    uint32_t vehicle_id,
    float speed,
    int control_mode,
    float longitude,
    float latitude,
    float altitude,
    float heading,
    float gear_position,
    int power_mode,
    uint64_t timestamp)
{
    // 1. 发布到FastDDS (调用父类方法)
    if (!FastDDSPublisher::publishVehicleStatus(topic, vehicle_id, speed, control_mode, longitude, latitude, altitude, heading, gear_position, power_mode, timestamp)) {
        return false;
    }

    // 2. 发布到MQTT (完整17个字段)
    uint64_t final_timestamp = timestamp == 0 ? getCurrentTimestamp() : timestamp;
    std::string mqtt_payload = createVehicleStatusMQTTPayload(
        vehicle_id, speed, control_mode, longitude, latitude, altitude, heading, gear_position, power_mode, final_timestamp);
    
    if (!publishToMQTT("/vehicle/vehicle_status", mqtt_payload)) {
        std::cerr << "MQTT发布车辆状态失败" << std::endl;
        return false;
    }

    if (g_verbose_output) {
        std::cout << "车辆状态已发布 (FastDDS + MQTT)" << std::endl;
    }
    return true;
}

bool FastDDSMQTTPublisher::publishRemoteControl(
    const std::string& topic,
    bool steering_angle_enable,
    float steering_angle,
    bool target_acceleration_enable,
    float target_acceleration,
    bool indicator_left_enable,
    bool indicator_left,
    bool indicator_right_enable,
    bool indicator_right,
    bool gear_position_enable,
    int32_t gear_position,
    bool ebrake_status_enable,
    int32_t ebrake_status,
    uint64_t timestamp)
{
    // 1. 发布到FastDDS (调用父类方法)
    if (!FastDDSPublisher::publishRemoteControl(topic, steering_angle_enable, steering_angle, target_acceleration_enable, target_acceleration, indicator_left_enable, indicator_left, indicator_right_enable, indicator_right, gear_position_enable, gear_position, ebrake_status_enable, ebrake_status, timestamp)) {
        return false;
    }

    // 2. 发布到MQTT (完整13个字段)
    uint64_t final_timestamp = timestamp == 0 ? getCurrentTimestamp() : timestamp;
    std::string mqtt_payload = createRemoteControlMQTTPayload(steering_angle_enable, steering_angle, target_acceleration_enable, target_acceleration, final_timestamp);
    
    if (!publishToMQTT("/vehicle/control_cmd", mqtt_payload)) {
        std::cerr << "MQTT发布远程控制失败" << std::endl;
        return false;
    }

    if (g_verbose_output) {
        std::cout << "远程控制已发布 (FastDDS + MQTT)" << std::endl;
    }
    return true;
}

bool FastDDSMQTTPublisher::publishToMQTT(const std::string& topic, const std::string& payload)
{
    if (!mqtt_bridge_) {
        std::cerr << "MQTT连接未初始化" << std::endl;
        return false;
    }

    mqtt_bridge_->publish_async(topic, payload);
    return true;  // 异步发布，假设成功
}

std::string FastDDSMQTTPublisher::createHandshakeRequestMQTTPayload(
    long noa_active, long override_status, long override_ready, uint64_t timestamp)
{
    return "{"
           "\"noa_active_request\":" + std::to_string(noa_active) + ","
           "\"remote_override_status\":" + std::to_string(override_status) + ","
           "\"remote_override_ready\":" + std::to_string(override_ready) + ","
           "\"timestamp\":" + std::to_string(timestamp) +
           "}";
}

std::string FastDDSMQTTPublisher::createHandshakeResponseMQTTPayload(
    long noa_active, long override_response, bool current_control_source, uint64_t timestamp)
{
    return "{"
           "\"noa_active_response\":" + std::to_string(noa_active) + ","
           "\"remote_override_response\":" + std::to_string(override_response) + ","
           "\"current_control_source\":" + (current_control_source ? "true" : "false") + ","
           "\"timestamp\":" + std::to_string(timestamp) +
           "}";
}

std::string FastDDSMQTTPublisher::createVehicleStatusMQTTPayload(
    uint32_t vehicle_id, float speed, int control_mode, float longitude, float latitude,
    float altitude, float heading, int gear_position, int power_mode, uint64_t timestamp)
{
    // 按照IDL定义的完整17个字段
    return "{"
           "\"vehicle_id\":" + std::to_string(vehicle_id) + ","
           "\"control_mode\":" + std::to_string(control_mode) + ","
           "\"position_longitude\":" + std::to_string(longitude) + ","
           "\"position_latitude\":" + std::to_string(latitude) + ","
           "\"position_altitude\":" + std::to_string(altitude) + ","
           "\"speed\":" + std::to_string(speed) + ","
           "\"yawrate\":0.0,"  // 默认值，可以后续添加参数
           "\"gear_position\":" + std::to_string(gear_position) + ","
           "\"acceleration\":0.0,"  // 默认值，可以后续添加参数
           "\"heading\":" + std::to_string(heading) + ","
           "\"steering_angle\":0.0,"  // 默认值，可以后续添加参数
           "\"wheel_angle\":0.0,"  // 默认值，可以后续添加参数
           "\"ebrake_status\":0,"  // 默认值，可以后续添加参数
           "\"indicator_left\":0,"  // 默认值，可以后续添加参数
           "\"indicator_right\":0,"  // 默认值，可以后续添加参数
           "\"timestamp\":" + std::to_string(timestamp) + ","
           "\"power_mode\":" + std::to_string(power_mode) +
           "}";
}

std::string FastDDSMQTTPublisher::createRemoteControlMQTTPayload(
    bool steering_enable, float steering_angle, bool accel_enable, float acceleration, uint64_t timestamp)
{
    // 按照IDL定义的完整13个字段
    return std::string("{") +
           "\"steering_angle_enable\":" + (steering_enable ? "true" : "false") + "," +
           "\"steering_angle\":" + std::to_string(steering_angle) + "," +
           "\"target_acceleration_enable\":" + (accel_enable ? "true" : "false") + "," +
           "\"target_acceleration\":" + std::to_string(acceleration) + "," +
           "\"indicator_left_enable\":false," +  // 默认值，可以后续添加参数
           "\"indicator_left\":false," +  // 默认值
           "\"indicator_right_enable\":false," +  // 默认值
           "\"indicator_right\":false," +  // 默认值
           "\"gear_position_enable\":false," +  // 默认值
           "\"gear_position\":2," +  // D档默认值
           "\"ebrake_status_enable\":false," +  // 默认值
           "\"ebrake_status\":0," +  // 默认值
           "\"timestamp\":" + std::to_string(timestamp) +
           "}";
}
