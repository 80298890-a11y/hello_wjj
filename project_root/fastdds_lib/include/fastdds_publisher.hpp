#ifndef FASTDDS_PUBLISHER_HPP
#define FASTDDS_PUBLISHER_HPP

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/core/policy/QosPolicies.hpp>


using namespace eprosima::fastdds::dds;

// 前向声明（在remote_msgs命名空间中）
namespace remote_msgs {
    class HandshakeRequest;
    class HandshakeResponse; 
    class VehicleStatus;
    class ControlCmd;
}



/**
 * @brief 通用的FastDDS发布者库
 * 
 * 提供简单易用的接口来发布各种类型的消息，支持默认值和自定义参数
 */
class FastDDSPublisher {
public:
    /**
     * @brief 构造函数
     * @param domain_id DDS域ID，默认为0
     */
    explicit FastDDSPublisher(int domain_id = 0);
    
    /**
     * @brief 析构函数
     */
    virtual ~FastDDSPublisher();

    /**
     * @brief 初始化发布者
     * @return 成功返回true，失败返回false
     */
    virtual bool init();

    /**
     * @brief 清理资源
     */
    void cleanup();

    // =========================== 握手消息发布 ===========================
    
    /**
     * @brief 发布握手请求消息
     * @param topic 主题名称，默认为"HandshakeRequestTopic"
     * @param noa_active NOA激活状态，默认为0
     * @param override_status 覆盖状态，默认为0  
     * @param override_ready 覆盖就绪状态，默认为0
     * @param timestamp 时间戳，默认为当前时间
     * @return 成功返回true，失败返回false
     */
    virtual bool publishHandshakeRequest(
        const std::string& topic = "/handshake/request",
        long noa_active = 0,
        long override_status = 0, 
        long override_ready = 0,
        uint64_t timestamp = 0  // 0表示使用当前时间
    );

    /**
     * @brief 发布握手响应消息
     * @param topic 主题名称，默认为"HandshakeResponseTopic"
     * @param noa_active NOA激活响应，默认为0
     * @param override_response 覆盖响应，默认为0
     * @param current_control_source 当前控制源，默认为true
     * @param timestamp 时间戳，默认为当前时间
     * @return 成功返回true，失败返回false
     */
    virtual bool publishHandshakeResponse(
        const std::string& topic = "/handshake/response",
        long noa_active = 0,
        long override_response = 0,
        bool current_control_source = true,
        uint64_t timestamp = 0
    );

    // =========================== 车辆状态消息发布 ===========================
    
    /**
     * @brief 发布车辆状态消息
     * @param topic 主题名称，默认为"VehicleStatusTopic"
     * @param vehicle_id 车辆ID，默认为1
     * @param speed 速度，默认为30.0
     * @param control_mode 控制模式，默认为0 (Auto)
     * @param longitude 经度，默认为116.3975
     * @param latitude 纬度，默认为39.9085
     * @param altitude 海拔，默认为50.0
     * @param heading 航向角，默认为45.0
     * @param gear_position 档位，默认为1
     * @param power_mode 电源模式，默认为2 (ON)
     * @param timestamp 时间戳，默认为当前时间
     * @return 成功返回true，失败返回false
     */
    virtual bool publishVehicleStatus(
        const std::string& topic = "/vehicle/vehicle_status",
        uint32_t vehicle_id = 1,
        float speed = 30.0f,
        int control_mode = 0,  // 0=Auto, 1=Manual, 2=Remote
        float longitude = 116.3975f,
        float latitude = 39.9085f, 
        float altitude = 50.0f,
        float heading = 45.0f,
        float gear_position = 1.0f,
        int power_mode = 2,  // 0=OFF, 1=ACC, 2=ON, 3=CRANK
        uint64_t timestamp = 0
    );

    // =========================== 远程控制消息发布 ===========================
    
    /**
     * @brief 发布远程控制消息
     * @param topic 主题名称，默认为"ControlCmdTopic"
     * @param steering_angle_enable 转向角使能，默认为true
     * @param steering_angle 转向角，默认为0.0
     * @param target_acceleration_enable 目标加速度使能，默认为true
     * @param target_acceleration 目标加速度，默认为0.0
     * @param indicator_left_enable 左转向灯使能，默认为false
     * @param indicator_left 左转向灯，默认为false
     * @param indicator_right_enable 右转向灯使能，默认为false
     * @param indicator_right 右转向灯，默认为false
     * @param gear_position_enable 档位使能，默认为true
     * @param gear_position 档位，默认为1
     * @param ebrake_status_enable 电子手刹使能，默认为true
     * @param ebrake_status 电子手刹状态，默认为0
     * @param remote_drive_ready 远程驾驶就绪，默认为false
     * @param remote_drive_takeover_request 远程驾驶接管请求，默认为false
     * @param timestamp 时间戳，默认为当前时间
     * @return 成功返回true，失败返回false
     */
    virtual bool publishRemoteControl(
        const std::string& topic = "/vehicle/control_cmd",
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

        uint64_t timestamp = 0
    );


    // =========================== 通用接口 ===========================
    
    /**
     * @brief 设置发送频率
     * @param frequency_hz 发送频率（Hz），默认100Hz
     */
    void setPublishFrequency(int frequency_hz = 100);

    /**
     * @brief 获取当前时间戳（毫秒）
     * @return 当前时间戳
     */
    static uint64_t getCurrentTimestamp();

private:
    // FastDDS 核心组件
    DomainParticipant* participant_;
    Publisher* publisher_;
    int domain_id_;
    
    // 主题和数据写入器的缓存
    std::unordered_map<std::string, Topic*> topics_;
    std::unordered_map<std::string, DataWriter*> writers_;
    std::unordered_map<std::string, TypeSupport> type_supports_;
    
    // 发送频率控制
    int publish_frequency_hz_;
    
    /**
     * @brief 创建或获取主题
     * @param topic_name 主题名称
     * @param type_name 类型名称
     * @param type_support 类型支持
     * @return 主题指针，失败返回nullptr
     */
    Topic* getOrCreateTopic(const std::string& topic_name, 
                           const std::string& type_name,
                           TypeSupport type_support);
    
    /**
     * @brief 创建或获取数据写入器
     * @param topic 主题
     * @return 数据写入器指针，失败返回nullptr
     */
    DataWriter* getOrCreateWriter(Topic* topic);
};

#endif // FASTDDS_PUBLISHER_HPP
