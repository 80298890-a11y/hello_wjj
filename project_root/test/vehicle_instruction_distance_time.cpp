/**
 * @file vehicle_instruction_distance_time.cpp
 * @brief 车辆距离和时间控制指令处理器
 * 
 * 功能:
 * - 订阅MQTT topic: /vehicle/instruction/distance/time/<vid>
 * - 接收云端距离和时间控制指令并立即回复received确认消息
 * - 执行加速-等待-减速的时序控制
 * - 完成后回复processed状态消息
 * 
 * 使用方式：
 * ./vehicle_instruction_distance_time
 */

 #include <iostream>
 #include <string>
 #include <thread>
 #include <atomic>
 #include <chrono>
 #include <signal.h>
 #include <mutex>
 #include <cstdlib>
 #include <iomanip>
 #include <yaml-cpp/yaml.h>
 #include <jsoncpp/json/json.h>
 #include <mosquitto.h>
 #include "logger_conf/universal_logger.h"
 
 // 全局变量
 std::atomic<bool> g_running{true};
 std::string g_vid;
 std::string g_subscribe_topic;
 std::string g_response_topic;
 struct mosquitto* g_mosq = nullptr;
 std::mutex g_mqtt_mutex;
 
 /**
  * @brief 信号处理函数
  */
void signal_handler(int signal) {
    static int signal_count = 0;
    signal_count++;
    
    if (signal_count == 1) {
        LOG_INFO("接收到停止信号，准备退出...");
        g_running = false;
    } else {
        LOG_INFO("强制退出...");
        std::_Exit(1);
    }
}
 
 /**
  * @brief 从vehicle_config.yaml读取车辆ID
  */
 std::string readVIDFromConfig() {
     try {
         YAML::Node config = YAML::LoadFile("../car_config/vehicle_config.yaml");
         if (config["vehicle"] && config["vehicle"]["vid"]) {
             return config["vehicle"]["vid"].as<std::string>();
         }
     } catch (...) {
         // 配置文件读取失败，使用默认值
     }
     return "10001001";
 }
 
 /**
  * @brief 获取当前时间戳(毫秒)
  */
 int64_t getCurrentTimestamp() {
     auto now = std::chrono::system_clock::now();
     auto duration = now.time_since_epoch();
     return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
 }
 
 /**
  * @brief 发送MQTT消息
  */
 bool sendMQTTMessage(const std::string& topic, const std::string& message) {
     std::lock_guard<std::mutex> lock(g_mqtt_mutex);
     
     if (!g_mosq) return false;
     
    int ret = mosquitto_publish(g_mosq, nullptr, topic.c_str(), message.length(), message.c_str(), 0, false);
    if (ret == MOSQ_ERR_SUCCESS) {
        LOG_INFO(std::string("发送响应: ") + message);
        return true;
    }
    return false;
 }
 
 /**
  * @brief 创建响应消息
  */
 std::string createResponse(const std::string& action, int64_t counter, int64_t timestamp, 
                           int64_t mqtt_timestamp, const std::string& error = "") {
     Json::Value response;
     response["action"] = action;
     response["counter"] = counter;
     response["timestamp"] = timestamp;
     response["mqtt_" + action + "_timestamp"] = mqtt_timestamp;
     
     if (!error.empty()) {
         response["error"] = error;
     }
     
     Json::StreamWriterBuilder builder;
     builder["indentation"] = "";
     return Json::writeString(builder, response);
 }
 
/**
 * @brief 发送加速度控制命令
 */
bool sendAccelerationCommand(double acceleration) {
    std::string command = "./mqtt_sender control_cmd target_acceleration_enable=1 target_acceleration=" + std::to_string(acceleration);
    LOG_DEBUG(std::string("执行命令: ") + command);
    std::cout << "[调用前时间戳] " << getCurrentTimestamp() << " ms - 执行命令: " << command << std::endl;
    int result = std::system(command.c_str());
    std::cout << "[完成后时间戳] " << getCurrentTimestamp() << " ms - 命令执行完成 (返回值: " << result << ")" << std::endl;
    LOG_DEBUG(std::string("系统调用返回值: ") + std::to_string(result));
    return result == 0;
}
 
 /**
  * @brief 执行距离和时间控制序列
  */
bool executeDistanceTimeControl(double acceleration, double acceleration_duration, double deceleration) {
    LOG_INFO(std::string("执行距离时间控制 - 加速: ") + std::to_string(acceleration) + ", 持续: " + std::to_string(acceleration_duration) + "s, 减速: " + std::to_string(deceleration));
     
     // 1. 发送加速命令
     if (!sendAccelerationCommand(acceleration)) {
         return false;
     }
     
    // 2. 等待指定持续时间
    auto wait_start = std::chrono::steady_clock::now();
    auto wait_duration = std::chrono::duration<double>(acceleration_duration);
    LOG_INFO(std::string("开始等待 ") + std::to_string(acceleration_duration) + " 秒...");
     
     int wait_count = 0;
    while (g_running) {
        auto elapsed = std::chrono::steady_clock::now() - wait_start;
        if (elapsed >= wait_duration) {
            LOG_INFO(std::string("等待完成，实际等待时间: ") + std::to_string(std::chrono::duration<double>(elapsed).count()) + " 秒");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_count++;
        if (wait_count % 50 == 0) { // 每5秒输出一次进度
            double elapsed_seconds = std::chrono::duration<double>(elapsed).count();
            std::ostringstream oss;
            oss << "等待中... 已等待: " << std::fixed << std::setprecision(1) << elapsed_seconds << "s";
            LOG_INFO(oss.str());
        }
    }
     
     if (!g_running) {
         return false;
     }
     
    // 3. 发送减速命令
    double deceleration_value = -std::abs(deceleration);
    LOG_INFO(std::string("等待完成，开始发送减速命令: ") + std::to_string(deceleration_value));
    bool result = sendAccelerationCommand(deceleration_value);
    LOG_INFO(std::string("减速命令发送") + (result ? "成功" : "失败"));
    
    // 4. 等待1秒后发送最终控制命令
    if (result) {
        LOG_INFO("等待1秒后发送最终控制命令...");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        result = sendAccelerationCommand(-1.5);
        LOG_INFO(std::string("最终控制命令发送") + (result ? "成功" : "失败"));
    }
    
    return result;
}
 
 /**
  * @brief 处理距离和时间控制消息
  */
 void processDistanceTimeMessage(const std::string& message) {
     Json::Value root;
     Json::Reader reader;
     
    if (!reader.parse(message, root) || 
        !root.isMember("acceleration") || !root.isMember("acceleration_duration") || 
        !root.isMember("deceleration") || !root.isMember("counter") || !root.isMember("timestamp")) {
        LOG_ERROR("消息格式错误");
        return;
    }
     
     double acceleration = root["acceleration"].asDouble();
     double acceleration_duration = root["acceleration_duration"].asDouble();
     double deceleration = root["deceleration"].asDouble();
     int64_t counter = root["counter"].asInt64();
     int64_t timestamp = root["timestamp"].asInt64();
     
     // 1. 发送"received"响应
     std::string received_response = createResponse("received", counter, timestamp, getCurrentTimestamp());
     if (!sendMQTTMessage(g_response_topic, received_response)) {
         return;
     }
     
     // 2. 执行距离和时间控制序列
     bool success = executeDistanceTimeControl(acceleration, acceleration_duration, deceleration);
     
    // 3. 发送"processed"响应
    std::string error = success ? "" : "距离时间控制执行失败";
    std::string processed_response = createResponse("processed", counter, timestamp, getCurrentTimestamp(), error);
    sendMQTTMessage(g_response_topic, processed_response);
    
    LOG_INFO("处理完成");
}
 
 /**
  * @brief MQTT消息回调函数
  */
 void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
     if (!message || !message->payload) return;
     
     std::string topic(message->topic);
     std::string payload(static_cast<char*>(message->payload), message->payloadlen);
     
     if (topic == g_subscribe_topic) {
         LOG_INFO(std::string("收到消息 [") + topic + "] " + payload);
         processDistanceTimeMessage(payload);
     }
 }
 
 /**
  * @brief MQTT连接回调函数
  */
void on_connect(struct mosquitto* mosq, void* userdata, int result) {
    if (result == 0) {
        LOG_INFO("MQTT连接成功");
        mosquitto_subscribe(mosq, nullptr, g_subscribe_topic.c_str(), 0);
    } else {
        LOG_ERROR("MQTT连接失败");
    }
}
 
 /**
  * @brief MQTT断连回调函数
  */
void on_disconnect(struct mosquitto* mosq, void* userdata, int result) {
    if (result != 0) {
        LOG_WARN("MQTT意外断开连接，尝试重连...");
        
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (mosquitto_reconnect(mosq) == MOSQ_ERR_SUCCESS) {
                LOG_INFO("MQTT重连成功");
                break;
            }
        }
    }
}
 
 /**
  * @brief 初始化MQTT客户端
  */
 bool initMQTT() {
     mosquitto_lib_init();
     
     std::string client_id = "vehicle_instruction_distance_time_8975" + g_vid;
     g_mosq = mosquitto_new(client_id.c_str(), true, nullptr);
     if (!g_mosq) return false;
     
     mosquitto_connect_callback_set(g_mosq, on_connect);
     mosquitto_message_callback_set(g_mosq, on_message);
     mosquitto_disconnect_callback_set(g_mosq, on_disconnect);
     
     if (mosquitto_connect(g_mosq, "hellorobotaxi.cn", 11883, 60) != MOSQ_ERR_SUCCESS) {
         return false;
     }
     
     return mosquitto_loop_start(g_mosq) == MOSQ_ERR_SUCCESS;
 }
 
 /**
  * @brief 清理MQTT资源
  */
 void cleanupMQTT() {
     if (g_mosq) {
         mosquitto_loop_stop(g_mosq, true);  // 强制停止
         mosquitto_destroy(g_mosq);
         g_mosq = nullptr;
     }
     mosquitto_lib_cleanup();
 }
 
int main() {
    std::cout << "车辆距离和时间控制指令处理器" << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化通用日志系统
    initUniversalLogger("vehicle_instruction_distance_time");
    LOG_INFO("通用日志系统启动");

    g_vid = readVIDFromConfig();
    g_subscribe_topic = "/vehicle/instruction/distance/time/" + g_vid;
    g_response_topic = "/vehicle/instruction/distance/time/" + g_vid + "/response";
    
    LOG_INFO(std::string("VID: ") + g_vid);

    if (!initMQTT()) {
        LOG_ERROR("MQTT初始化失败");
        cleanupUniversalLogger();
        return 1;
    }

    LOG_INFO("开始监听距离和时间控制指令...");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    cleanupMQTT();
    LOG_INFO("程序已退出");
    cleanupUniversalLogger();
    return 0;
}