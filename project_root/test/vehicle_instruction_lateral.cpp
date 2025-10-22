/**
 * @file vehicle_instruction_lateral.cpp
 * @brief 车辆横向控制指令处理器
 * 
 * 功能:
 * - 订阅MQTT topic: /vehicle/instruction/lateral/<vid>
 * - 接收云端横向控制指令并立即回复received确认消息
 * - 执行渐进式转向控制（每10ms步进1度直到达到传入的目标角度）
 * - 完成后回复processed状态消息，然后程序结束
 * 
 * 使用方式：
 * ./vehicle_instruction_lateral
 */

 #include <iostream>
 #include <string>
 #include <thread>
 #include <atomic>
 #include <chrono>
 #include <signal.h>
 #include <mutex>
 #include <cstdlib>
 #include <cmath>
 #include <yaml-cpp/yaml.h>
 #include <jsoncpp/json/json.h>
 #include <mosquitto.h>
 #include <sstream>
 #include "logger_conf/universal_logger.h"
 
// 全局变量
std::atomic<bool> g_running{true};
std::string g_vid;
std::string g_subscribe_topic;
std::string g_response_topic;
std::string g_vehicle_status_topic;
struct mosquitto* g_mosq = nullptr;
std::mutex g_mqtt_mutex;
std::atomic<double> g_current_steering_angle{0.0};
 
/**
 * @brief 信号处理函数
 */
void signal_handler(int signal) {
    LOG_INFO("接收到停止信号，准备退出...");
    g_running = false;
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
   int ret;
   {
       std::lock_guard<std::mutex> lock(g_mqtt_mutex);
       if (!g_mosq) return false;
       
       ret = mosquitto_publish(g_mosq, nullptr, topic.c_str(), message.length(), message.c_str(), 0, false);
   }
   
   if (ret == MOSQ_ERR_SUCCESS) {
       LOG_INFO(std::string("发送: ") + message);
       return true;
   }
   return false;
}
 
 /**
  * @brief 创建响应消息
  */
 std::string createResponse(const std::string& action, int64_t counter, int64_t timestamp, 
                           int64_t mqtt_timestamp, int left_or_right = -1, double steering_angle = 0.0, 
                           const std::string& error = "") {
     std::ostringstream oss;
     oss << "{\"action\":" << action << ",\"counter\":" << counter 
         << ",\"timestamp\":" << timestamp 
         << ",\"mqtt_" << action << "_timestamp\":" << mqtt_timestamp;
     
     if (left_or_right >= 0) {
         oss << ",\"left_or_right\":" << left_or_right 
             << ",\"steering_angle\":" << steering_angle;
     }
     
     if (!error.empty()) {
         oss << ",\"error\":\"" << error << "\"";
     }
     
     oss << "}";
     return oss.str();
 }
 
/**
 * @brief 发送单次转向命令
 */
bool sendSteeringCommand(double angle) {
   Json::Value control_msg;
   control_msg["steering_angle_enable"] = 1;
   // 将角度四舍五入为整数，避免长浮点数
   control_msg["steering_angle"] = static_cast<int>(std::round(angle));
   
   Json::StreamWriterBuilder builder;
   builder["indentation"] = "";
   std::string control_str = Json::writeString(builder, control_msg);
   std::string control_topic = "/vehicle/control_cmd/" + g_vid;
   
   std::cout << "[发送前时间戳] " << getCurrentTimestamp() << " ms - 发送转向命令: angle=" << angle << std::endl;
   
   int ret;
   {
       std::lock_guard<std::mutex> lock(g_mqtt_mutex);
       if (!g_mosq) return false;
       
       ret = mosquitto_publish(g_mosq, nullptr, control_topic.c_str(), 
                                  control_str.length(), control_str.c_str(), 0, false);
   }
   
   std::cout << "[发送后时间戳] " << getCurrentTimestamp() << " ms - 转向命令发送完成 (返回值: " << ret << ")" << std::endl;
   
   if (ret == MOSQ_ERR_SUCCESS) {
    //    std::cout << ">>> 转发控制命令 >>>" << std::endl;
    //    std::cout << "    Topic: " << control_topic << std::endl;
    //    std::cout << "    Payload: " << control_str << std::endl;
       g_current_steering_angle = angle;
       return true;
   }
   LOG_ERROR(std::string("发送失败: ") + mosquitto_strerror(ret));
   return false;
}
 
 /**
 * @brief 执行渐进式转向控制
 */
bool executeGradualSteering(double target_angle) {
   double current_angle = g_current_steering_angle.load();
   
   LOG_INFO(std::string("开始转向: 当前=") + std::to_string(current_angle) + 
            "度 → 目标=" + std::to_string(target_angle) + "度");
    
   // 如果已经在目标位置，直接返回
   if (std::abs(target_angle - current_angle) <= 0.5) {
       LOG_INFO("已在目标位置，无需转动");
       return true;
   }
    
   double step = (target_angle > current_angle) ? 1.0 : -1.0;
   int step_count = 0;
   
   // 渐进式控制循环
   while (g_running) {
       // 检查是否已达到目标角度
       if (std::abs(target_angle - current_angle) <= 1.0) {
           if (!sendSteeringCommand(target_angle)) return false;
           LOG_INFO(std::string("转向完成，步数: ") + std::to_string(step_count + 1));
           break;
       }
       
       current_angle += step;
       step_count++;
       
       if (!sendSteeringCommand(current_angle)) {
           LOG_ERROR("发送转向命令失败");
           return false;
       }
       
       LOG_INFO(std::string("[") + std::to_string(step_count) + "] " + 
                std::to_string(current_angle) + "度 (剩余: " + 
                std::to_string(std::abs(target_angle - current_angle)) + "度)");
        
       // 等待10ms
       for (int i = 0; i < 10 && g_running; ++i) {
           std::this_thread::sleep_for(std::chrono::milliseconds(1));
       }
       
       if (!g_running) return false;
   }
    
   return g_running;
}
 
/**
 * @brief 处理车辆状态消息 - 同步实际转向角度
 */
void processVehicleStatus(const Json::Value& root) {
   if (root.isMember("steering_angle")) {
       double steering_angle_rad = root["steering_angle"].asDouble();
       // 弧度转角度: 角度 = 弧度 × (180 / π)
       g_current_steering_angle = steering_angle_rad * (180.0 / M_PI);
   }
}

/**
 * @brief 处理横向控制消息
 */
void processLateralMessage(const Json::Value& root) {
   int left_or_right = root["left_or_right"].asInt();
   double steering_angle = root["steering_angle"].asDouble();
   int64_t counter = root["counter"].asInt64();
   int64_t timestamp = root["timestamp"].asInt64();
   
   double current_angle = g_current_steering_angle.load();
   
   // 打印当前全局转向角度
   std::cout << ">>> g_current_steering_angle = " << current_angle << " 度" << std::endl;
   
   // 根据左右方向计算目标角度：基于当前角度的累加
   // 左转 (0): 当前角度 + 转向角度
   // 右转 (1): 当前角度 - 转向角度
   double target_angle = (left_or_right == 0) ? 
                       (current_angle + steering_angle) : 
                       (current_angle - steering_angle);
   
   LOG_INFO(std::string("指令: ") + (left_or_right == 0 ? "左转" : "右转") + 
            std::to_string(steering_angle) + "度, 目标=" + std::to_string(target_angle) + "度");
    
   // 1. 发送"received"响应
   std::string received_response = createResponse("received", counter, timestamp, getCurrentTimestamp());
   if (!sendMQTTMessage(g_response_topic, received_response)) return;
    
   // 2. 执行渐进式转向控制
   bool success = executeGradualSteering(target_angle);
    
   // 3. 发送"processed"响应
   std::string error = success ? "" : "转向控制执行失败";
   std::string processed_response = createResponse("processed", counter, timestamp, getCurrentTimestamp(),
                                                 left_or_right, g_current_steering_angle.load(), error);
   sendMQTTMessage(g_response_topic, processed_response);
   LOG_INFO("等待下一条指令...");
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
       std::cout << "\n========== 收到转向指令 ==========" << std::endl;
       auto now = std::chrono::system_clock::now();
       auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
       std::cout << "[接收时间戳] " << ms << " ms" << std::endl;
       std::cout << "Topic: " << topic << std::endl;
       std::cout << "Payload: " << payload << std::endl;
       std::cout << "==================================\n" << std::endl;
       
       Json::Value root;
       Json::Reader reader;
       
       if (!reader.parse(payload, root) || 
           !root.isMember("left_or_right") || !root.isMember("steering_angle") || 
           !root.isMember("counter") || !root.isMember("timestamp")) {
           LOG_ERROR("消息格式错误");
           return;
       }
       
       processLateralMessage(root);
   } else if (topic == g_vehicle_status_topic) {
       Json::Value root;
       Json::Reader reader;
       
       if (reader.parse(payload, root)) {
           processVehicleStatus(root);
       }
   }
}
 
 /**
 * @brief MQTT连接回调函数
 */
void on_connect(struct mosquitto* mosq, void* userdata, int result) {
   if (result == 0) {
       mosquitto_subscribe(mosq, nullptr, g_subscribe_topic.c_str(), 0);
       mosquitto_subscribe(mosq, nullptr, g_vehicle_status_topic.c_str(), 0);
       LOG_INFO("MQTT连接成功，等待指令...");
   } else {
       LOG_ERROR("MQTT连接失败");
       g_running = false;
   }
}
 
 /**
 * @brief 初始化MQTT客户端
 */
bool initMQTT() {
    mosquitto_lib_init();
    
    g_mosq = mosquitto_new(("vehicle_instruction_lateral_8975" + g_vid).c_str(), true, nullptr);
    if (!g_mosq) return false;
    
    mosquitto_connect_callback_set(g_mosq, on_connect);
    mosquitto_message_callback_set(g_mosq, on_message);
    
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
         mosquitto_loop_stop(g_mosq, true);
         mosquitto_destroy(g_mosq);
         g_mosq = nullptr;
     }
     mosquitto_lib_cleanup();
 }
 
int main() {
   std::cout << "车辆横向控制指令处理器" << std::endl;

   signal(SIGINT, signal_handler);
   signal(SIGTERM, signal_handler);

   initUniversalLogger("vehicle_instruction_lateral");

   g_vid = readVIDFromConfig();
   g_subscribe_topic = "/vehicle/instruction/lateral/" + g_vid;
   g_response_topic = "/vehicle/instruction/lateral/" + g_vid + "/response";
   g_vehicle_status_topic = "/vehicle/vehicle_status";
   
   LOG_INFO(std::string("VID: ") + g_vid);

   if (!initMQTT()) {
       LOG_ERROR("MQTT初始化失败");
       cleanupUniversalLogger();
       return 1;
   }

   // 持续运行，直到收到退出信号
   while (g_running) {
       std::this_thread::sleep_for(std::chrono::milliseconds(100));
   }

   cleanupMQTT();
   LOG_INFO("程序已退出");
   cleanupUniversalLogger();
   
   return 0;
}