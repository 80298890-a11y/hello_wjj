/**
 * @file mqtt_to_fastdds_converter.hpp
 * @brief MQTT到FastDDS转换器通用库头文件
 * 
 * 功能：
 * - 订阅MQTT消息
 * - 解析JSON数据
 * - 转换为FastDDS IDL消息
 * - 发布到FastDDS域
 * 
 * 设计：
 * - 继承自FastDDSPublisher，复用发布功能
 * - 集成MQTT客户端功能
 * - 提供JSON解析和数据转换接口
 */

#pragma once

#include "fastdds_publisher.hpp"
#include <mosquitto.h>
#include <functional>
#include <string>
#include <map>
#include <memory>
#include <atomic>
#include <json/json.h>

/**
 * @brief MQTT到FastDDS转换器类
 * 
 * 继承自FastDDSPublisher，增加MQTT订阅和JSON转换功能
 */
class MQTTToFastDDSConverter : public FastDDSPublisher {
public:
    // MQTT消息回调函数类型定义 (topic, message, conversion_success)
    using MQTTMessageCallback = std::function<void(const std::string& topic, const std::string& message, bool conversion_success)>;

    /**
     * @brief 构造函数
     * @param domain_id FastDDS域ID
     * @param mqtt_host MQTT服务器地址
     * @param mqtt_port MQTT服务器端口
     * @param client_id MQTT客户端ID
     */
    explicit MQTTToFastDDSConverter(
        int domain_id = 0,
        const std::string& mqtt_host = "hellorobotaxi.cn", 
        int mqtt_port = 11883,
        const std::string& client_id = "mqtt_to_fastdds_converter"
    );

    /**
     * @brief 析构函数
     */
    virtual ~MQTTToFastDDSConverter();

    /**
     * @brief 初始化转换器
     * @return 成功返回true，失败返回false
     */
    virtual bool init() override;

    /**
     * @brief 清理资源
     */
    void cleanup();

    // =========================== MQTT订阅功能 ===========================

    /**
     * @brief 订阅MQTT主题
     * @param topic MQTT主题名称
     * @param qos QoS级别，默认为0
     * @return 成功返回true，失败返回false
     */
    bool subscribeMQTTTopic(const std::string& topic, int qos = 0);

    /**
     * @brief 订阅所有预定义的MQTT主题
     * @return 成功返回true，失败返回false
     */
    bool subscribeAllMQTTTopics();

    /**
     * @brief 设置MQTT消息回调
     * @param callback 回调函数
     */
    void setMQTTMessageCallback(MQTTMessageCallback callback);

    // =========================== JSON转换功能 ===========================

    /**
     * @brief 处理MQTT消息并转换为FastDDS消息
     * @param topic MQTT主题
     * @param json_message JSON格式的消息内容
     * @return 成功返回true，失败返回false
     */
    bool processAndConvert(const std::string& topic, const std::string& json_message);

    /**
     * @brief 解析JSON字符串
     * @param json_str JSON字符串
     * @param json_value 解析结果
     * @return 成功返回true，失败返回false
     */
    static bool parseJSON(const std::string& json_str, Json::Value& json_value);

    /**
     * @brief 从JSON中安全获取整数值
     * @param json JSON对象
     * @param key 键名
     * @param default_value 默认值
     * @return 获取的值或默认值
     */
    static long getJSONLong(const Json::Value& json, const std::string& key, long default_value = 0);

    /**
     * @brief 从JSON中安全获取浮点值
     * @param json JSON对象
     * @param key 键名
     * @param default_value 默认值
     * @return 获取的值或默认值
     */
    static float getJSONFloat(const Json::Value& json, const std::string& key, float default_value = 0.0f);

    /**
     * @brief 从JSON中安全获取布尔值
     * @param json JSON对象
     * @param key 键名
     * @param default_value 默认值
     * @return 获取的值或默认值
     */
    static bool getJSONBool(const Json::Value& json, const std::string& key, bool default_value = false);

    /**
     * @brief 从JSON中安全获取无符号整数值
     * @param json JSON对象
     * @param key 键名
     * @param default_value 默认值
     * @return 获取的值或默认值
     */
    static uint32_t getJSONUInt32(const Json::Value& json, const std::string& key, uint32_t default_value = 0);

    /**
     * @brief 从JSON中安全获取时间戳值
     * @param json JSON对象
     * @param key 键名
     * @param default_value 默认值
     * @return 获取的值或默认值
     */
    static uint64_t getJSONUInt64(const Json::Value& json, const std::string& key, uint64_t default_value = 0);

    // =========================== 特定消息转换功能 ===========================

    /**
     * @brief 转换握手请求消息
     * @param json JSON对象
     * @return 成功返回true，失败返回false
     */
    bool convertHandshakeRequest(const Json::Value& json);

    /**
     * @brief 转换握手响应消息
     * @param json JSON对象
     * @return 成功返回true，失败返回false
     */
    bool convertHandshakeResponse(const Json::Value& json);

    /**
     * @brief 转换车辆状态消息
     * @param json JSON对象
     * @return 成功返回true，失败返回false
     */
    bool convertVehicleStatus(const Json::Value& json);

    /**
     * @brief 转换远程控制消息
     * @param json JSON对象
     * @return 成功返回true，失败返回false
     */
    bool convertRemoteControl(const Json::Value& json);



    // =========================== 状态查询 ===========================

    /**
     * @brief 检查MQTT客户端是否已连接
     * @return 已连接返回true，否则返回false
     */
    bool isMQTTConnected() const;

    /**
     * @brief 获取MQTT连接状态描述
     * @return 状态描述字符串
     */
    std::string getMQTTStatusString() const;

protected:
    // =========================== 内部辅助方法 ===========================

    /**
     * @brief 初始化MQTT客户端
     * @return 成功返回true，失败返回false
     */
    bool initMQTT();

    /**
     * @brief 清理MQTT客户端
     */
    void cleanupMQTT();

    /**
     * @brief MQTT连接回调
     */
    static void onMQTTConnect(struct mosquitto* mosq, void* obj, int result);

    /**
     * @brief MQTT断开连接回调
     */
    static void onMQTTDisconnect(struct mosquitto* mosq, void* obj, int result);

    /**
     * @brief MQTT消息接收回调
     */
    static void onMQTTMessage(struct mosquitto* mosq, void* obj, const struct mosquitto_message* message);

    /**
     * @brief MQTT订阅回调
     */
    static void onMQTTSubscribe(struct mosquitto* mosq, void* obj, int mid, int qos_count, const int* granted_qos);

private:
    // MQTT相关成员变量
    struct mosquitto* mqtt_client_;
    std::string mqtt_host_;
    int mqtt_port_;
    std::string client_id_;
    std::atomic<bool> mqtt_connected_{false};
    std::atomic<int> subscriptions_confirmed_{0};
    
    // 回调函数
    MQTTMessageCallback message_callback_;
    
    // 预定义的MQTT主题列表
    static const std::vector<std::string> DEFAULT_MQTT_TOPICS;
};
