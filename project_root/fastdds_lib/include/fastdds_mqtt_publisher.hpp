#ifndef FASTDDS_MQTT_PUBLISHER_HPP
#define FASTDDS_MQTT_PUBLISHER_HPP

#include "fastdds_publisher.hpp"
#include <memory>

// 前向声明
class MQTTMosquitto;

/**
 * @brief 重构后的FastDDS + MQTT集成发布者 (继承FastDDSPublisher)
 * 
 * 通过继承FastDDSPublisher避免代码重复，只添加MQTT功能
 */
class FastDDSMQTTPublisher : public FastDDSPublisher {
public:
    /**
     * @brief 构造函数
     * @param domain_id FastDDS域ID
     * @param mqtt_broker MQTT服务器地址
     * @param mqtt_port MQTT服务器端口
     */
    FastDDSMQTTPublisher(
        int domain_id,
        const std::string& mqtt_broker,
        int mqtt_port);

    /**
     * @brief 析构函数
     */
    virtual ~FastDDSMQTTPublisher();

    /**
     * @brief 初始化发布者 (FastDDS + MQTT)
     * @return 成功返回true，失败返回false
     */
    bool init() override;

    /**
     * @brief 发布握手请求 (FastDDS + MQTT)
     */
    bool publishHandshakeRequest(
        const std::string& topic,
        long noa_active,
        long override_status = 0,
        long override_ready = 1,
        uint64_t timestamp = 0) override;

    /**
     * @brief 发布握手响应 (FastDDS + MQTT)
     */
    bool publishHandshakeResponse(
        const std::string& topic,
        long noa_active,
        long override_response = 1,
        bool current_control_source = true,
        uint64_t timestamp = 0) override;

    /**
     * @brief 发布车辆状态 (FastDDS + MQTT)
     */
    bool publishVehicleStatus(
        const std::string& topic = "VehicleStatusTopic",
        uint32_t vehicle_id = 1,
        float speed = 30.0f,
        int control_mode = 0,
        float longitude = 116.3975f,
        float latitude = 39.9085f, 
        float altitude = 50.0f,
        float heading = 45.0f,
        float gear_position = 1.0f,
        int power_mode = 2,
        uint64_t timestamp = 0) override;

    /**
     * @brief 发布远程控制命令 (FastDDS + MQTT)
     */
    bool publishRemoteControl(
        const std::string& topic = "ControlCmdTopic",
        bool steering_angle_enable = true,
        float steering_angle = 0.0f,
        bool target_acceleration_enable = true,
        float target_acceleration = 0.0f,
        bool indicator_left_enable = false,
        bool indicator_left = false,
        bool indicator_right_enable = false,
        bool indicator_right = false,
        bool gear_position_enable = true,
        int32_t gear_position = 1,
        bool ebrake_status_enable = true,
        int32_t ebrake_status = 0,
        uint64_t timestamp = 0) override;

private:
    std::string mqtt_broker_;
    int mqtt_port_;
    std::unique_ptr<MQTTMosquitto> mqtt_bridge_;

    /**
     * @brief 发布MQTT消息
     */
    bool publishToMQTT(const std::string& topic, const std::string& payload);

    /**
     * @brief 创建握手请求的MQTT载荷
     */
    std::string createHandshakeRequestMQTTPayload(
        long noa_active, long override_status, long override_ready, uint64_t timestamp);

    /**
     * @brief 创建握手响应的MQTT载荷
     */
    std::string createHandshakeResponseMQTTPayload(
        long noa_active, long override_response, bool current_control_source, uint64_t timestamp);

    /**
     * @brief 创建车辆状态的MQTT载荷 (完整17个字段)
     */
    std::string createVehicleStatusMQTTPayload(
        uint32_t vehicle_id, float speed, int control_mode, float longitude, float latitude,
        float altitude, float heading, int gear_position, int power_mode, uint64_t timestamp);

    /**
     * @brief 创建远程控制的MQTT载荷 (完整13个字段)
     */
    std::string createRemoteControlMQTTPayload(
        bool steering_enable, float steering_angle, bool accel_enable, float acceleration, uint64_t timestamp);
};

#endif // FASTDDS_MQTT_PUBLISHER_REFACTORED_HPP
