#include "ConfigManager.h"
#include <fstream>
#include <iostream>
#include <sstream>

ConfigManager::ConfigManager() {}

ConfigManager::~ConfigManager() {}

bool ConfigManager::loadConfig(const std::string& config_file_path) {
    config_file_path_ = config_file_path;
    
    std::ifstream config_file(config_file_path);
    if (!config_file.is_open()) {
        std::cerr << "[ConfigManager] 无法打开配置文件: " << config_file_path << std::endl;
        return false;
    }
    
    Json::Reader reader;
    if (!reader.parse(config_file, root_)) {
        std::cerr << "[ConfigManager] 解析配置文件失败: " << reader.getFormattedErrorMessages() << std::endl;
        return false;
    }
    
    std::cout << "[ConfigManager] 成功加载配置文件: " << config_file_path << std::endl;
    return true;
}

Json::Value ConfigManager::getValue(const std::string& path) const {
    Json::Value current = root_;
    std::istringstream ss(path);
    std::string token;
    
    while (std::getline(ss, token, '.')) {
        if (current.isMember(token)) {
            current = current[token];
        } else {
            return Json::Value::null;
        }
    }
    
    return current;
}

std::string ConfigManager::getString(const std::string& path, const std::string& default_value) const {
    Json::Value value = getValue(path);
    if (value.isNull() || !value.isString()) {
        return default_value;
    }
    return value.asString();
}

int ConfigManager::getInt(const std::string& path, int default_value) const {
    Json::Value value = getValue(path);
    if (value.isNull() || !value.isInt()) {
        return default_value;
    }
    return value.asInt();
}

double ConfigManager::getDouble(const std::string& path, double default_value) const {
    Json::Value value = getValue(path);
    if (value.isNull() || !value.isNumeric()) {
        return default_value;
    }
    return value.asDouble();
}

bool ConfigManager::getBool(const std::string& path, bool default_value) const {
    Json::Value value = getValue(path);
    if (value.isNull() || !value.isBool()) {
        return default_value;
    }
    return value.asBool();
}

std::map<std::string, double> ConfigManager::getParamsMap(const std::string& path) const {
    std::map<std::string, double> result;
    Json::Value value = getValue(path);
    
    if (value.isNull() || !value.isObject()) {
        return result;
    }
    
    for (const auto& key : value.getMemberNames()) {
        if (value[key].isNumeric()) {
            result[key] = value[key].asDouble();
        }
    }
    
    return result;
}

std::vector<std::string> ConfigManager::getStringArray(const std::string& path) const {
    std::vector<std::string> result;
    Json::Value value = getValue(path);
    
    if (value.isNull() || !value.isArray()) {
        return result;
    }
    
    for (const auto& item : value) {
        if (item.isString()) {
            result.push_back(item.asString());
        }
    }
    
    return result;
}

std::map<std::string, std::string> ConfigManager::getStringMap(const std::string& path) const {
    std::map<std::string, std::string> result;
    Json::Value value = getValue(path);
    
    if (value.isNull() || !value.isObject()) {
        return result;
    }
    
    for (const auto& key : value.getMemberNames()) {
        if (value[key].isString()) {
            result[key] = value[key].asString();
        }
    }
    
    return result;
}

ConfigManager::MQTTConfig ConfigManager::getMQTTConfig(const std::string& component_name) const {
    MQTTConfig config;
    
    // 先获取通用MQTT配置
    config.host = getString("mqtt_config.host", "localhost");
    config.port = getInt("mqtt_config.port", 1883);
    config.keepalive = getInt("mqtt_config.keepalive", 60);
    
    // 如果指定了组件名，尝试获取组件特定的client_id
    if (!component_name.empty()) {
        std::string client_id_path = component_name + ".client_id";
        config.client_id = getString(client_id_path, component_name + "_client");
    } else {
        config.client_id = "default_mqtt_client";
    }
    
    return config;
}

ConfigManager::FastDDSConfig ConfigManager::getFastDDSConfig(const std::string& component_name) const {
    FastDDSConfig config;
    
    config.domain_id = getInt("fastdds_config.domain_id", 0);
    
    if (!component_name.empty()) {
        std::string participant_name_path = component_name + ".participant_name";
        config.participant_name = getString(participant_name_path, 
            getString("fastdds_config.participant_name", "DefaultParticipant"));
    } else {
        config.participant_name = getString("fastdds_config.participant_name", "DefaultParticipant");
    }
    
    return config;
}

ConfigManager::LoggingConfig ConfigManager::getLoggingConfig() const {
    LoggingConfig config;
    
    config.log_base_dir = getString("logging_config.log_base_dir", "./logs");
    config.log_level = getString("logging_config.log_level", "INFO");
    config.max_queue_size = getInt("logging_config.max_queue_size", 10000);
    
    return config;
}

void ConfigManager::printConfig() const {
    std::cout << "\n=== 配置信息 ===" << std::endl;
    std::cout << "配置文件: " << config_file_path_ << std::endl;
    
    auto mqtt_config = getMQTTConfig();
    std::cout << "MQTT服务器: " << mqtt_config.host << ":" << mqtt_config.port << std::endl;
    
    auto fastdds_config = getFastDDSConfig();
    std::cout << "FastDDS域: " << fastdds_config.domain_id << std::endl;
    
    auto logging_config = getLoggingConfig();
    std::cout << "日志目录: " << logging_config.log_base_dir << std::endl;
    std::cout << "================" << std::endl;
}
