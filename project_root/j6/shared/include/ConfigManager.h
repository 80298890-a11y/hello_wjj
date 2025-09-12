#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <map>
#include <vector>
#include <json/json.h>

class ConfigManager {
private:
    Json::Value root_;
    std::string config_file_path_;
    
public:
    ConfigManager();
    ~ConfigManager();
    
    bool loadConfig(const std::string& config_file_path);
    
    // 基础类型获取
    std::string getString(const std::string& path, const std::string& default_value = "") const;
    int getInt(const std::string& path, int default_value = 0) const;
    double getDouble(const std::string& path, double default_value = 0.0) const;
    bool getBool(const std::string& path, bool default_value = false) const;
    
    // 复合类型获取
    std::map<std::string, double> getParamsMap(const std::string& path) const;
    std::vector<std::string> getStringArray(const std::string& path) const;
    std::map<std::string, std::string> getStringMap(const std::string& path) const;
    
    // MQTT配置
    struct MQTTConfig {
        std::string host;
        int port;
        int keepalive;
        std::string client_id;
    };
    MQTTConfig getMQTTConfig(const std::string& component_name = "") const;
    
    // FastDDS配置
    struct FastDDSConfig {
        int domain_id;
        std::string participant_name;
    };
    FastDDSConfig getFastDDSConfig(const std::string& component_name = "") const;
    
    // 日志配置
    struct LoggingConfig {
        std::string log_base_dir;
        std::string log_level;
        int max_queue_size;
    };
    LoggingConfig getLoggingConfig() const;
    
    // 打印配置信息
    void printConfig() const;
    
private:
    Json::Value getValue(const std::string& path) const;
};

#endif // CONFIG_MANAGER_H
