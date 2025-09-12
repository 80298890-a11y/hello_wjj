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
    class StateMachineState;
    class FaultReport;
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


    // =========================== 车辆状态消息发布 ===========================
    


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
        int32_t lane_change = 0,
        uint64_t timestamp = 0
    );



    // =========================== 通用接口 ===========================
    
    /**
     * @brief 设置发送频率
     * @param frequency_hz 发送频率（Hz），默认100Hz
     */
    void setPublishFrequency(int frequency_hz = 100);

    /**
     * @brief 发布握手响应消息 (车端发送)
     * @param topic 主题名称，默认为 "/handshake/response"
     * @param noa_active_response NOA激活响应
     * @param remote_override_response 远程接管响应
     * @param current_control_source 当前控制源
     * @param timestamp 时间戳
     * @return 发布成功返回true，失败返回false
     */
    virtual bool publishHandshakeResponse(
        const std::string& topic = "/handshake/response",
        long noa_active_response = 0,
        long remote_override_response = 0,
        bool current_control_source = false,
        uint64_t timestamp = 0
    );

    /**
     * @brief 发布车辆状态消息 (车端发送)
     * @param topic 主题名称，默认为 "/vehicle/vehicle_status"
     * @param longitude 经度
     * @param latitude 纬度
     * @param heading 航向角
     * @param speed 速度
     * @param steering_angle 转向角
     * @param gear_position 档位
     * @param timestamp 时间戳
     * @return 发布成功返回true，失败返回false
     */
    virtual bool publishVehicleStatus(
        const std::string& topic = "/vehicle/vehicle_status",
        double longitude = 0.0,
        double latitude = 0.0,
        double heading = 0.0,
        float speed = 0.0f,
        float steering_angle = 0.0f,
        float gear_position = 0.0f,
        uint64_t timestamp = 0
    );

    /**
     * @brief 发布驾驶状态消息 (车端发送)
     * @param topic 主题名称，默认为 "/vehicle/drive_state"
     * @param driving_state 驾驶状态
     * @return 发布成功返回true，失败返回false
     */
    virtual bool publishDriveState(
        const std::string& topic = "/vehicle/drive_state",
        long driving_state = 0
    );

    /**
     * @brief 发布故障报告消息 (车端发送)
     * @param topic 主题名称，默认为 "/vehicle/fault_report"
     * @param fault_types 故障类型数组
     * @return 发布成功返回true，失败返回false
     */
    virtual bool publishFaultReport(
        const std::string& topic = "/vehicle/fault_report",
        const std::vector<long>& fault_types = {}
    );

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
