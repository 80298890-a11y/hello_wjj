# 通用日志管理系统

## 🎯 概述

这是一个统一的日志管理系统，为所有C++程序提供：
- **自动日志清理**：超过指定大小自动删除最旧的日志
- **配置化管理**：通过YAML文件统一配置所有程序的日志行为
- **异步日志**：不阻塞主线程的高性能日志记录
- **多级别日志**：支持DEBUG、INFO、WARN、ERROR四个级别

## 📁 文件结构

```
logger_conf/
├── log_config.yaml          # 日志配置文件
├── universal_logger.h       # 日志系统头文件
├── universal_logger.cpp     # 日志系统实现
└── README.md               # 本文档
```

## 🚀 使用方法

### 1. 在C++程序中使用

```cpp
#include "logger_conf/universal_logger.h"

int main() {
    // 初始化日志系统
    initUniversalLogger("your_program_name");
    
    // 使用日志宏
    LOG_INFO("程序启动");
    LOG_DEBUG("调试信息");
    LOG_WARN("警告信息");
    LOG_ERROR("错误信息");
    
    // 程序结束前清理
    cleanupUniversalLogger();
    return 0;
}
```

### 2. 编译设置

```bash
# 编译通用日志系统
g++ -c logger_conf/universal_logger.cpp -o universal_logger.o -std=c++17

# 链接到你的程序
g++ -o your_program your_program.cpp universal_logger.o -I. -std=c++17 -lpthread
```

### 3. 配置文件说明

在 `log_config.yaml` 中为你的程序添加配置：

```yaml
programs:
  your_program_name:
    subdir: "your_program"        # 日志子目录
    level: "INFO"                 # 日志级别
    max_files: 20                 # 最大文件数
```

## 📊 配置参数详解

### 全局配置 (global)
- `max_storage_mb`: 最大存储大小(MB)，默认50MB
- `base_dir`: 日志根目录，默认"logs"
- `auto_cleanup`: 是否自动清理，默认true
- `cleanup_interval_minutes`: 清理检查间隔(分钟)，默认10分钟

### 程序配置 (programs.program_name)
- `subdir`: 日志子目录名
- `level`: 日志级别 (DEBUG/INFO/WARN/ERROR)
- `max_files`: 保留的最大文件数

## 📈 支持的程序列表

当前配置支持以下程序：
- `bridge_forwarder`: 桥接转发器
- `mqtt_sender`: MQTT发送器  
- `mqtt_receiver`: MQTT接收器
- `subscriber`: FastDDS订阅器
- `publisher`: FastDDS发布器
- `mqtt_to_fastdds_bridge`: MQTT到FastDDS桥接器
- `fastdds_to_mqtt_bridge`: FastDDS到MQTT桥接器
- `data_processor`: 数据处理器
- `tsp_forwarder`: TSP转发器
- `mqtt_simulator`: MQTT模拟器

## 🛠️ 迁移现有程序

### 步骤1：替换头文件
```cpp
// 旧代码
#include "async_logger.hpp"
std::unique_ptr<AsyncLogger> g_logger;

// 新代码  
#include "logger_conf/universal_logger.h"
```

### 步骤2：替换初始化
```cpp
// 旧代码
g_logger = std::make_unique<AsyncLogger>("program_name");

// 新代码
initUniversalLogger("program_name");
```

### 步骤3：替换日志调用
```cpp
// 旧代码
g_logger->log("INFO", "消息内容");

// 新代码
LOG_INFO("消息内容");
```

### 步骤4：添加清理
```cpp
// 在main函数结束前添加
cleanupUniversalLogger();
```

## 🔧 日志自动清理机制

- **监控机制**：后台线程每10分钟检查一次日志大小
- **清理策略**：当日志总大小超过配置的限制时，删除最旧的日志文件
- **保护机制**：正在使用的日志文件不会被删除
- **状态文件保护**：特殊文件（如.mqtt_sender_state.json）不会被清理

## 📝 日志格式

```
[2025-09-08 15:05:57.362] [INFO] 消息内容
[时间戳]                   [级别] [消息]
```

## ⚠️ 注意事项

1. **配置文件路径**：程序会自动查找 `logger_conf/log_config.yaml` 或 `../logger_conf/log_config.yaml`
2. **线程安全**：日志系统是线程安全的，可以在多线程环境中使用
3. **性能影响**：使用异步日志，对主线程性能影响极小
4. **磁盘空间**：自动清理机制确保日志不会无限制增长

## 🎉 优势

✅ **统一管理**：所有程序使用相同的日志配置和格式  
✅ **自动清理**：无需手动管理日志文件大小  
✅ **高性能**：异步日志不阻塞主线程  
✅ **易于调试**：统一的时间戳和格式便于问题排查  
✅ **配置灵活**：每个程序可以有独立的日志配置


