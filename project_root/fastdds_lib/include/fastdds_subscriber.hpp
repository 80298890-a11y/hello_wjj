#ifndef FASTDDS_SUBSCRIBER_HPP
#define FASTDDS_SUBSCRIBER_HPP

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#include <fastrtps/Domain.h>
#include <fastrtps/participant/Participant.h>
#include <fastrtps/attributes/ParticipantAttributes.h>
#include <fastrtps/subscriber/Subscriber.h>
#include <fastrtps/attributes/SubscriberAttributes.h>
#include <fastrtps/subscriber/SubscriberListener.h>
#include <fastrtps/TopicDataType.h>

using namespace eprosima::fastrtps;

// 前向声明（在remote_msgs命名空间中）
namespace remote_msgs {
    class HandshakeRequest;
    class HandshakeResponse; 
    class VehicleStatus;
    class ControlCmd;
}



/**
 * @brief 通用的FastDDS订阅者库
 * 
 * 提供简单易用的接口来订阅各种类型的消息，支持回调函数
 */
class FastDDSSubscriber {
public:
    // 回调函数类型定义
    using HandshakeRequestCallback = std::function<void(long noa_active, long override_status, long override_ready, uint64_t timestamp)>;
    using HandshakeResponseCallback = std::function<void(long noa_active, long override_response, bool control_source, uint64_t timestamp)>;
    using VehicleStatusCallback = std::function<void(uint32_t vehicle_id, int control_mode, float position_longitude, float position_latitude, float position_altitude, float speed, float yawrate, float gear_position, float acceleration, float heading, float steering_angle, float wheel_angle, long ebrake_status, long indicator_left, long indicator_right, uint64_t timestamp, int power_mode)>;
    using RemoteControlCallback = std::function<void(bool steering_angle_enable, float steering_angle, bool target_acceleration_enable, float target_acceleration, bool indicator_left_enable, bool indicator_left, bool indicator_right_enable, bool indicator_right, bool gear_position_enable, long gear_position, bool ebrake_status_enable, long ebrake_status, uint64_t timestamp)>;


    /**
     * @brief 构造函数
     * @param domain_id DDS域ID，默认为0
     */
    explicit FastDDSSubscriber(int domain_id = 0);
    
    /**
     * @brief 析构函数
     */
    virtual ~FastDDSSubscriber();

    /**
     * @brief 初始化订阅者
     * @return 成功返回true，失败返回false
     */
    virtual bool init();

    /**
     * @brief 清理资源
     */
    void cleanup();

    // =========================== 回调函数设置 ===========================
    
    /**
     * @brief 设置握手请求消息回调
     */
    void setHandshakeRequestCallback(HandshakeRequestCallback callback);

    /**
     * @brief 设置握手响应消息回调
     */
    void setHandshakeResponseCallback(HandshakeResponseCallback callback);

    /**
     * @brief 设置车辆状态消息回调
     */
    void setVehicleStatusCallback(VehicleStatusCallback callback);

    /**
     * @brief 设置远程控制消息回调
     */
    void setRemoteControlCallback(RemoteControlCallback callback);



    // =========================== 订阅功能 ===========================

    /**
     * @brief 订阅握手请求消息
     * @param topic 主题名称，默认为"HandshakeRequestTopic"
     * @return 成功返回true，失败返回false
     */
    virtual bool subscribeHandshakeRequest(const std::string& topic = "/handshake/request");

    /**
     * @brief 订阅握手响应消息
     * @param topic 主题名称，默认为"HandshakeResponseTopic"
     * @return 成功返回true，失败返回false
     */
    virtual bool subscribeHandshakeResponse(const std::string& topic = "/handshake/response");

    /**
     * @brief 订阅车辆状态消息
     * @param topic 主题名称，默认为"VehicleStatusTopic"
     * @return 成功返回true，失败返回false
     */
    virtual bool subscribeVehicleStatus(const std::string& topic = "/vehicle/vehicle_status");

    /**
     * @brief 订阅远程控制消息
     * @param topic 主题名称，默认为"RemoteControlTopic"
     * @return 成功返回true，失败返回false
     */
    virtual bool subscribeRemoteControl(const std::string& topic = "/vehicle/control_cmd");

    /**
     * @brief 订阅所有主题
     * @return 成功返回true，失败返回false
     */
    bool subscribeAllTopics();

    /**
     * @brief 开始监听消息
     * @return 成功返回true，失败返回false
     */
    bool startListening();
    
    /**
     * @brief 停止监听消息
     */
    void stopListening();
    
    // 方法名简化映射
    bool subscribeToHandshakeRequest(const std::string& topic = "/handshake/request");
    bool subscribeToHandshakeResponse(const std::string& topic = "/handshake/response"); 
    bool subscribeToVehicleStatus(const std::string& topic = "/vehicle/vehicle_status");
    bool subscribeToRemoteControl(const std::string& topic = "/vehicle/control_cmd");

protected:
    // =========================== 内部辅助方法 ===========================
    
    // 暂时注释复杂方法，使用 FastRTPS 2.6.10 简化实现
    // /**
    //  * @brief 获取或创建主题
    //  */
    // Topic* getOrCreateTopic(const std::string& topic_name, const std::string& type_name, TypeSupport& type_support);
    
    // /**
    //  * @brief 获取或创建数据读取器
    //  */
    // DataReader* getOrCreateReader(Topic* topic, DataReaderListener* listener);

    // =========================== 监听器类 ===========================
    
    class HandshakeRequestListener;
    class HandshakeResponseListener;
    class VehicleStatusListener;
    class RemoteControlListener;


private:
    // FastRTPS 实体
    Participant* participant_;
    eprosima::fastrtps::Subscriber* subscriber_;
    int domain_id_;

    // 主题和读取器管理 - 暂时注释，FastRTPS 2.6.10 API 不同
    // std::unordered_map<std::string, Topic*> topics_;
    // std::unordered_map<std::string, DataReader*> readers_;
    // std::unordered_map<std::string, TypeSupport> type_supports_;
    
    // 监听器实例 - 暂时注释，FastRTPS 2.6.10 API 不同
    // std::unordered_map<std::string, std::unique_ptr<DataReaderListener>> listeners_;

    // 回调函数
    HandshakeRequestCallback handshake_request_callback_;
    HandshakeResponseCallback handshake_response_callback_;
    VehicleStatusCallback vehicle_status_callback_;
    RemoteControlCallback remote_control_callback_;

};

#endif // FASTDDS_SUBSCRIBER_HPP
