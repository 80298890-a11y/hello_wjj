# FastDDS ↔ MQTT 多阶段转发通信系统

本项目实现了一个完整的MQTT多阶段转发链路，支持车端到云端的复杂消息流转，最终通过FastDDS进行本地高性能通信。

## 🎯 项目概述

### 核心功能
- **多阶段MQTT转发**: 支持复杂的消息路由和处理链路
- **动态VID支持**: 支持任意车辆ID的独立数据流
- **状态管理**: 智能的状态变化检测和lastvalue机制
- **高频转发**: 100Hz持续转发确保数据实时性
- **异步日志**: 结构化日志记录，便于调试和监控
- **TSP命令处理**: 云端TSP指令到车载系统的完整转换

### 系统架构
```
🌐 云端MQTT (tsp/command/{vid}) 
    ↓
🔄 TSP转发器 (tsp_forwarder)
    ↓ (cloud/noa/{vid})
🎭 MQTT模拟器 (mqtt_simulator) 
    ↓ (zhan/send/{vid})
🧠 数据处理器 (data_processor)
    ↓ (/handshake/request/noa)
🌉 桥接转发器 (bridge_forwarder, 100Hz)
    ↓ (/handshake/request)
🔗 MQTT→FastDDS转换器 (mqtt_to_fastdds_bridge)
    ↓ (FastDDS)
🚗 车载系统订阅器 (subscriber)
```

## 📁 项目结构

```
project_root/
├── 📋 idl/                          # IDL接口定义文件
│   ├── Handshake.idl               # 握手消息定义
│   ├── VehicleStatus.idl           # 车辆状态定义
│   ├── RemoteControl.idl           # 远程控制定义
│   └── TSPCommand.idl              # TSP命令定义
│
├── 🔧 generated_types/              # 自动生成的类型文件
│   ├── Handshake/                  # 握手消息C++类
│   ├── VehicleStatus/              # 车辆状态C++类
│   ├── RemoteControl/              # 远程控制C++类
│   └── TSPCommand/                 # TSP命令C++类
│
├── 📚 fastdds_lib/                  # 核心功能库（已优化简化）
│   ├── include/                    # 库头文件
│   │   ├── fastdds_publisher.hpp           # FastDDS发布器
│   │   ├── fastdds_subscriber.hpp          # FastDDS订阅器
│   │   ├── fastdds_mqtt_publisher.hpp      # FastDDS+MQTT一体化发布器
│   │   └── mqtt_to_fastdds_converter.hpp   # MQTT→FastDDS转换器
│   ├── src/                        # 库实现文件
│   ├── README.md                   # 库详细说明文档
│   └── CMakeLists.txt              # 库构建配置
│
├── 🎯 test/                         # 应用程序目录
│   ├── 核心转发链路程序/
│   │   ├── tsp_forwarder.cpp           # TSP转发器（MQTT程序1）
│   │   ├── mqtt_simulator.cpp          # MQTT模拟器（MQTT程序X）
│   │   ├── data_processor.cpp          # 数据处理器（MQTT程序2）
│   │   ├── bridge_forwarder.cpp        # 桥接转发器（MQTT程序4，100Hz）
│   │   └── mqtt_to_fastdds_bridge.cpp  # MQTT→FastDDS转换器
│   │
│   ├── FastDDS程序/
│   │   ├── publisher.cpp               # FastDDS消息发布器
│   │   └── subscriber.cpp              # FastDDS消息订阅器
│   │
│   ├── 测试辅助程序/
│   │   ├── mqtt_sender.cpp             # MQTT测试发送器
│   │   ├── mqtt_receiver.cpp           # MQTT测试接收器
│   │   ├── test_action_stop.cpp        # 停止动作测试器
│   │   └── fastdds_to_mqtt_bridge.cpp  # FastDDS→MQTT桥接器
│   │
│   ├── 配置文件/
│   │   ├── async_logger.hpp            # 异步日志系统
│   │   ├── CMakeLists.txt              # 应用构建配置
│   │   └── 启动流程说明.txt              # 详细启动指南
│   │
│   └── logs/                       # 运行日志目录
│       ├── tsp_forwarder/              # TSP转发器日志
│       ├── mqtt_simulator/             # MQTT模拟器日志
│       ├── data_processor/             # 数据处理器日志
│       ├── bridge_forwarder/           # 桥接转发器日志
│       ├── fastdds_mqtt/               # MQTT-FastDDS转换日志
│       └── fastdds/                    # FastDDS通信日志
│
└── 📖 README.md                     # 本文档
```

## 🚀 快速开始

### 1. 环境准备

```bash
# 安装依赖
sudo apt update
sudo apt install -y build-essential cmake
sudo apt install -y libfastdds-dev libmosquitto-dev libjsoncpp-dev mosquitto-clients

# 克隆项目
cd /path/to/your/workspace
```

### 2. 编译项目

```bash
# 编译核心库
cd fastdds_lib/build
cmake .. && make -j4

# 编译应用程序
cd ../../test/build
cmake .. && make -j4
```

### 3. 完整链路启动

按照以下顺序启动6个程序（6个终端窗口）：

```bash
# 终端1: FastDDS接收端
cd /home/wjj/work/project_root/test
./subscriber

# 终端2: MQTT→FastDDS转换器
./mqtt_to_fastdds_bridge

# 终端3: 桥接转发器（100Hz）
./bridge_forwarder

# 终端4: 数据处理器
./data_processor

# 终端5: MQTT模拟器
./mqtt_simulator

# 终端6: TSP转发器
./tsp_forwarder
```

### 4. 发送测试命令

```bash
# 使用mosquitto_pub发送TSP命令
mosquitto_pub -h hellorobotaxi.cn -p 11883 -t "tsp/command/789" -m '{"action":"start","module":"noa","timestamp":1755591299000}'

# 发送停止命令
mosquitto_pub -h hellorobotaxi.cn -p 11883 -t "tsp/command/789" -m '{"action":"stop","module":"noa","timestamp":1755591299000}'
```

## 💻 核心程序详解

### MQTT转发链路程序

#### 1. TSP转发器 (`tsp_forwarder.cpp`)
- **作用**: 接收云端TSP命令，提取VID并转发
- **订阅**: `tsp/command/+` (支持任意VID)
- **转发**: `cloud/noa/{vid}`
- **功能**: 添加MQTT发送时间戳

#### 2. MQTT模拟器 (`mqtt_simulator.cpp`)
- **作用**: 模拟服务器交互的中间环节
- **订阅**: `cloud/noa/+`
- **转发**: `zhan/send/{vid}`
- **功能**: 添加模拟器时间戳

#### 3. 数据处理器 (`data_processor.cpp`)
- **作用**: 处理action字段，管理noa_active状态
- **订阅**: `zhan/send/+`
- **功能**: 
  - 解析action ("start"/"stop")
  - 更新noa_active状态 (start→1, stop→0)
  - 维护完整的HandshakeRequest状态
  - 仅在状态变化时发布到 `/handshake/request/noa`

#### 4. 桥接转发器 (`bridge_forwarder.cpp`)
- **作用**: 100Hz高频转发，确保数据实时性
- **订阅**: `/handshake/request/noa`
- **转发**: `/handshake/request` (100Hz持续发送)
- **特性**: 使用lastvalue机制，即使无状态变化也持续转发

#### 5. MQTT→FastDDS转换器 (`mqtt_to_fastdds_bridge.cpp`)
- **作用**: 将MQTT消息转换为FastDDS消息
- **订阅**: 4个生产环境必要主题
  - `/handshake/request`
  - `/handshake/response`
  - `/vehicle/vehicle_status`
  - `/vehicle/control_cmd`
- **功能**: JSON解析和FastDDS类型转换

### FastDDS程序

#### 6. FastDDS订阅器 (`subscriber.cpp`)
- **作用**: 接收FastDDS消息，验证完整链路
- **订阅**: 所有FastDDS主题
- **功能**: 显示接收到的消息内容和统计信息

## 🔄 完整数据流转

### 多阶段MQTT转发链路

```
1. 云端发送: tsp/command/{vid}
   └─ 消息: {"action":"start","module":"noa","timestamp":cloud_time}

2. TSP转发器: tsp/command/{vid} → cloud/noa/{vid}
   └─ 添加: mqtt_send_timestamp

3. MQTT模拟器: cloud/noa/{vid} → zhan/send/{vid}
   └─ 添加: simulator_timestamp

4. 数据处理器: zhan/send/{vid} → /handshake/request/noa
   └─ 处理: action → noa_active, 维护完整状态
   └─ 仅在状态变化时发布

5. 桥接转发器: /handshake/request/noa → /handshake/request
   └─ 100Hz持续转发，使用lastvalue机制

6. MQTT→FastDDS转换器: /handshake/request → FastDDS
   └─ JSON → HandshakeRequest类型转换

7. FastDDS订阅器: 接收HandshakeRequest消息
   └─ 验证完整链路，显示最终结果
```

### VID独立性

每个VID（如123、456、789）都有独立的数据流：
- 并发处理多个VID
- 独立的状态管理
- 互不干扰的消息路由

## 📊 支持的消息类型

### IDL定义的消息类型

| 消息类型 | FastDDS Topic | MQTT Topic | 用途 |
|---------|---------------|------------|------|
| HandshakeRequest | `/handshake/request` | `/handshake/request` | 握手请求，包含noa_active状态 |
| HandshakeResponse | `/handshake/response` | `/handshake/response` | 握手响应 |
| VehicleStatus | `/vehicle/vehicle_status` | `/vehicle/vehicle_status` | 车辆状态 |
| RemoteControl | `/vehicle/control_cmd` | `/vehicle/control_cmd` | 远程控制 |

### TSP命令格式

#### 输入格式 (云端发送)
```json
{
  "action": "start",        // "start" 或 "stop"
  "module": "noa",         // 模块名称
  "timestamp": 1755591299000  // 云端时间戳
}
```

#### 最终HandshakeRequest格式
```json
{
  "noa_active": 1,         // 0=停止, 1=启动 (根据action转换)
  "override_status": 0,    // 覆盖状态
  "override_ready": 1,     // 覆盖就绪
  "timestamp": 1755678760602  // 当前时间戳
}
```

## 🛠️ 系统管理

### 进程管理

#### 启动所有程序
详细启动流程请查看：`test/启动流程说明.txt`

#### 清理所有进程
```bash
# 一键清理所有相关进程
pkill -f "(mqtt_to_fastdds_bridge|bridge_forwarder|data_processor|mqtt_simulator|tsp_forwarder|subscriber|publisher|mqtt_sender|test_action_stop)"

# 验证清理结果
ps aux | grep -E "(mqtt|fastdds|bridge|data_processor|simulator|forwarder|subscriber|publisher)" | grep -v grep

# 强制清理（如果需要）
pkill -9 -f "(mqtt_to_fastdds_bridge|bridge_forwarder|data_processor|mqtt_simulator|tsp_forwarder|subscriber|publisher|mqtt_sender|test_action_stop)"
```

### 日志系统

#### 日志目录结构
```
logs/
├── tsp_forwarder/          # TSP转发器日志
├── mqtt_simulator/         # MQTT模拟器日志  
├── data_processor/         # 数据处理器日志
├── bridge_forwarder/       # 桥接转发器日志
├── fastdds_mqtt/           # MQTT-FastDDS转换日志
└── fastdds/               # FastDDS通信日志
    ├── pub/               # 发布日志
    └── sub/               # 订阅日志
```

#### 实时日志监控
```bash
# 监控特定程序日志
tail -f logs/data_processor/$(ls -t logs/data_processor/ | head -1)

# 监控所有转发器日志
tail -f logs/*/$(ls -t logs/*/ | head -1)
```

## 🔧 故障排除

### 常见问题诊断

#### 1. 链路中断检查
```bash
# 检查所有进程状态
ps aux | grep -E "(mqtt|fastdds|bridge|data_processor|simulator|forwarder)" | grep -v grep

# 按顺序检查各程序日志
tail -5 logs/tsp_forwarder/*.log
tail -5 logs/data_processor/*.log  
tail -5 logs/bridge_forwarder/*.log
```

#### 2. 状态不变化问题
- **原因**: 发送相同的action（如连续发送"start"）
- **解决**: 发送不同的action触发状态变化
```bash
# 先发送stop，再发送start
mosquitto_pub -h hellorobotaxi.cn -p 11883 -t "tsp/command/789" -m '{"action":"stop","module":"noa","timestamp":1755591299000}'
mosquitto_pub -h hellorobotaxi.cn -p 11883 -t "tsp/command/789" -m '{"action":"start","module":"noa","timestamp":1755591299000}'
```

#### 3. 100Hz转发问题
- **检查**: bridge_forwarder是否使用默认lastvalue状态
- **预期**: 即使无状态更新，也应持续以100Hz转发
- **验证**: 查看mqtt_to_fastdds_bridge和subscriber的日志

#### 4. MQTT连接失败
```bash
# 检查网络连接
ping hellorobotaxi.cn
telnet hellorobotaxi.cn 11883

# 检查mosquitto客户端
mosquitto_pub -h hellorobotaxi.cn -p 11883 -t "test" -m "hello"
```

### 性能优化

#### 发送频率调整
```bash
# 低频测试 (减少网络压力)
mosquitto_pub -h hellorobotaxi.cn -p 11883 -t "tsp/command/789" -m '{"action":"start","module":"noa","timestamp":1755591299000}'

# 批量测试不同VID
for vid in 123 456 789 1000; do
  mosquitto_pub -h hellorobotaxi.cn -p 11883 -t "tsp/command/$vid" -m '{"action":"start","module":"noa","timestamp":1755591299000}'
done
```

#### 系统资源监控
```bash
# 查看程序资源使用
top -p $(pgrep -f "bridge_forwarder|data_processor")

# 监控网络流量
sudo netstat -i  # 查看网络接口统计
```

## 🎯 项目特色

### 技术优势
1. **多阶段转发**: 复杂的消息路由和处理链路
2. **动态VID支持**: 支持任意车辆ID的独立处理
3. **智能状态管理**: lastvalue机制确保数据连续性
4. **高频实时性**: 100Hz持续转发保证实时性
5. **异步日志系统**: 高效的非阻塞日志记录
6. **代码简化优化**: 移除不必要代码，提升性能

### 系统可靠性
1. **状态持久化**: 即使无新输入，系统也保持100Hz输出
2. **独立VID流**: 多车辆并发处理，互不干扰
3. **完整错误处理**: 全链路错误检测和恢复
4. **灵活扩展性**: 支持新VID和新消息类型

## 📚 相关文档

- **详细启动指南**: `test/启动流程说明.txt`
- **FastDDS库文档**: `fastdds_lib/README.md`
- **IDL定义文档**: `idl/README.md`
- **FastDDS官方文档**: https://fast-dds.docs.eprosima.com/
- **MQTT协议规范**: https://mqtt.org/

## 🔄 更新日志

### 2025-01-20 - 重大架构优化
- ✅ **新增完整MQTT转发链路**: 6程序协同工作的完整数据流
- ✅ **动态VID支持**: 支持任意车辆ID（789、1000等）的独立处理
- ✅ **智能状态管理**: lastvalue机制，100Hz持续转发
- ✅ **代码简化**: 清理200+行不必要的TSP处理代码
- ✅ **异步日志系统**: 高效的程序专用日志目录
- ✅ **进程管理优化**: 一键启动和清理命令

---

**项目维护**: 本项目实现了工业级的多阶段MQTT转发通信系统，特别适用于车载系统中云端指令到车载执行器的复杂数据流转场景，确保高实时性和系统可靠性。