# FastDDS ↔ MQTT 双向桥接通信系统

本项目实现了FastDDS与MQTT协议之间的双向消息转换，支持高性能本地通信与远程云端通信的无缝桥接。

## 🎯 项目概述

### 核心功能
- **双向桥接**: FastDDS ↔ MQTT 消息双向转换
- **高性能**: 支持100Hz+高频消息传输
- **类型安全**: 基于IDL的强类型消息定义
- **异步日志**: 结构化日志记录，便于调试和监控
- **灵活配置**: 支持自定义频率、持续时间、消息内容

### 应用场景
```
🚗 车载系统 (FastDDS) ←→ 🌉 桥接器 ←→ ☁️ 云端服务器 (MQTT)
   高性能本地通信              数据转换              远程互联网通信
```

## 📁 项目结构

```
project_root/
├── 📋 idl/                          # IDL接口定义文件
│   ├── Handshake.idl               # 握手消息定义
│   ├── VehicleStatus.idl           # 车辆状态定义
│   └── RemoteControl.idl           # 远程控制定义
│
├── 🔧 generated_types/              # 自动生成的类型文件
│   ├── Handshake/                  # 握手消息C++类
│   ├── VehicleStatus/              # 车辆状态C++类
│   └── RemoteControl/              # 远程控制C++类
│
├── 📚 fastdds_lib/                  # 核心功能库
│   ├── include/                    # 库头文件
│   │   ├── fastdds_publisher.hpp           # FastDDS发布器
│   │   ├── fastdds_subscriber.hpp          # FastDDS订阅器
│   │   ├── fastdds_mqtt_publisher.hpp      # FastDDS+MQTT一体化发布器
│   │   └── mqtt_to_fastdds_converter.hpp   # MQTT→FastDDS转换器
│   ├── src/                        # 库实现文件
│   └── CMakeLists.txt              # 库构建配置
│
├── 🎯 test/                         # 应用程序目录
│   ├── publisher.cpp               # FastDDS消息发布器
│   ├── subscriber.cpp              # FastDDS消息订阅器
│   ├── mqtt_sender.cpp             # MQTT消息发送器 (支持自定义模式)
│   ├── mqtt_receiver.cpp           # MQTT消息接收器
│   ├── fastdds_to_mqtt_bridge.cpp  # FastDDS→MQTT桥接器
│   ├── mqtt_to_fastdds_bridge.cpp  # MQTT→FastDDS桥接器
│   ├── build.sh                    # 构建脚本
│   ├── demo_direction1.sh          # Direction 1演示脚本
│   ├── test_direction2.sh          # Direction 2测试脚本
│   └── logs/                       # 运行日志目录
│
└── 📖 README.md                     # 本文档
```

## 🚀 快速开始

### 1. 环境准备

```bash
# 安装依赖
sudo apt update
sudo apt install -y build-essential cmake
sudo apt install -y libfastdds-dev libmosquitto-dev libjsoncpp-dev

# 克隆项目
cd /path/to/your/workspace
```

### 2. 编译项目

```bash
# 编译核心库
cd fastdds_lib/build
cmake .. && make -j4

# 编译应用程序
cd ../../test
./build.sh
```

### 3. 运行测试

#### Direction 1: FastDDS → MQTT
```bash
# 终端1: 启动FastDDS订阅器
./subscriber

# 终端2: 启动桥接器
./fastdds_to_mqtt_bridge

# 终端3: 发送FastDDS消息
./publisher
```

#### Direction 2: MQTT → FastDDS
```bash
# 终端1: 启动FastDDS订阅器
./subscriber

# 终端2: 启动桥接器
./mqtt_to_fastdds_bridge

# 终端3: 发送MQTT消息
./mqtt_sender custom handshake_request 100 5 noa_active=1 override_status=2
```

## 💻 核心程序详解

### 1. FastDDS发布器 (`publisher.cpp`)
**功能**: 以100Hz频率发送4种类型的FastDDS消息
```bash
./publisher  # 连续发送模式，4个topic轮流发送
```

### 2. MQTT发送器 (`mqtt_sender.cpp`)
**功能**: 灵活的MQTT消息发送器，支持三种模式

```bash
# 连续发送模式 (默认)
./mqtt_sender

# 单次发送模式
./mqtt_sender handshake_request noa_active=1 override_status=2

# 自定义发送模式
./mqtt_sender custom <topic> <frequency_hz> <duration_sec> [params...]

# 示例：5Hz发送3秒握手请求
./mqtt_sender custom handshake_request 5 3 noa_active=1 override_status=2

# 示例：50Hz发送10秒车辆状态
./mqtt_sender custom vehicle_status 50 10 vehicle_id=12345 speed=60.5
```

### 3. 桥接器程序

#### FastDDS → MQTT桥接器 (`fastdds_to_mqtt_bridge.cpp`)
- 订阅FastDDS消息
- 转换为JSON格式
- 发送到MQTT服务器 (hellorobotaxi.cn:11883)

#### MQTT → FastDDS桥接器 (`mqtt_to_fastdds_bridge.cpp`)
- 订阅MQTT消息
- 解析JSON数据
- 转换为FastDDS消息并发布

### 4. 订阅器程序

#### FastDDS订阅器 (`subscriber.cpp`)
- 接收所有FastDDS消息
- 显示消息内容和统计信息
- 生成详细的接收日志

#### MQTT接收器 (`mqtt_receiver.cpp`)
- 接收MQTT消息
- 显示JSON内容
- 多线程异步处理

## 📊 消息类型和格式

### 支持的消息类型

| 消息类型 | FastDDS Topic | MQTT Topic | 用途 |
|---------|---------------|------------|------|
| HandshakeRequest | `/handshake/request` | `/handshake/request` | 握手请求 |
| HandshakeResponse | `/handshake/response` | `/handshake/response` | 握手响应 |
| VehicleStatus | `/vehicle/vehicle_status` | `/vehicle/vehicle_status` | 车辆状态 |
| RemoteControl | `/vehicle/control_cmd` | `/vehicle/control_cmd` | 远程控制 |

### 消息格式示例

#### 握手请求 (HandshakeRequest)
```json
{
  "noa_active": 1,
  "override_status": 0,
  "override_ready": 1,
  "mqtt_send_timestamp": 1754996843930
}
```

#### 车辆状态 (VehicleStatus)
```json
{
  "vehicle_id": 12345,
  "speed": 60.5,
  "control_mode": 2,
  "position_longitude": 116.397,
  "position_latitude": 39.916,
  "mqtt_send_timestamp": 1754996843930
}
```

## 🔄 数据流转过程

### Direction 1: FastDDS → MQTT
```
publisher.cpp
    ↓ (FastDDS消息)
fastdds_to_mqtt_bridge.cpp
    ↓ (JSON转换)
MQTT服务器 (hellorobotaxi.cn:11883)
    ↓
mqtt_receiver.cpp (可选)
```

### Direction 2: MQTT → FastDDS
```
mqtt_sender.cpp
    ↓ (MQTT消息)
mqtt_to_fastdds_bridge.cpp
    ↓ (FastDDS转换)
subscriber.cpp
```

## 📝 日志系统

### 日志目录结构
```
logs/
├── fastdds/
│   ├── pub/时间戳/topic组/topic名.log     # FastDDS发送日志
│   └── sub/时间戳/topic组/topic名.log     # FastDDS接收日志
├── mqtt/
│   ├── send/时间戳/topic组/topic名.log    # MQTT发送日志
│   └── receive/时间戳/topic组/topic名.log # MQTT接收日志
└── fastdds_mqtt/
    ├── fastdds_to_mqtt/时间戳/            # Direction 1桥接日志
    └── mqtt_to_fastdds/时间戳/             # Direction 2桥接日志
```

### 日志内容示例
```
[2025-08-12 19:07:23.930] MQTT发送成功 [/handshake/request] {"noa_active":1,"override_status":2,"mqtt_send_timestamp":1754996843930}
```

## 🛠️ 开发指南

### 添加新的消息类型

1. **定义IDL结构**
```idl
// 在idl/NewMessage.idl中定义
struct NewMessage {
    long field1;
    double field2;
    unsigned long long timestamp;
};
```

2. **更新库配置**
```cmake
# 在fastdds_lib/CMakeLists.txt中添加
set(IDL_FILES
    ${IDL_SOURCE_DIR}/Handshake.idl
    ${IDL_SOURCE_DIR}/VehicleStatus.idl
    ${IDL_SOURCE_DIR}/RemoteControl.idl
    ${IDL_SOURCE_DIR}/NewMessage.idl  # 新增
)
```

3. **扩展发布器接口**
```cpp
// 在fastdds_publisher.hpp中添加
bool publishNewMessage(const std::string& topic, long field1, double field2, uint64_t timestamp);
```

4. **重新编译**
```bash
cd fastdds_lib/build && make -j4
cd ../../test && ./build.sh
```

### 修改现有消息

1. 编辑对应的IDL文件
2. 重新编译项目
3. 更新相关的发布和订阅代码

### 性能调优

#### 发送频率调整
```bash
# 高频发送 (200Hz)
./mqtt_sender custom handshake_request 200 10 noa_active=1

# 低频发送 (1Hz)
./mqtt_sender custom vehicle_status 1 60 vehicle_id=12345
```

#### QoS设置
- FastDDS: 默认RELIABLE模式，保证消息送达
- MQTT: 默认QoS=0，最多发送一次

## 🔧 故障排除

### 常见问题

1. **编译错误**
```bash
# 清理并重新编译
rm -rf fastdds_lib/build test/build
cd fastdds_lib/build && cmake .. && make -j4
cd ../../test && ./build.sh
```

2. **MQTT连接失败**
```bash
# 检查网络连接
ping hellorobotaxi.cn
telnet hellorobotaxi.cn 11883
```

3. **FastDDS通信问题**
```bash
# 检查防火墙设置
sudo ufw status
# 确保FastDDS端口未被阻塞
```

4. **消息丢失**
- 检查发送和接收程序的启动顺序
- 查看日志文件确认消息传输情况
- 调整发送频率避免网络拥塞

### 调试技巧

1. **查看实时日志**
```bash
# 监控MQTT发送日志
tail -f logs/mqtt/send/*/handshake/request.log

# 监控FastDDS接收日志
tail -f logs/fastdds/sub/*/handshake/request.log
```

2. **网络抓包**
```bash
# 抓取MQTT流量
sudo tcpdump -i any port 11883

# 抓取FastDDS流量
sudo tcpdump -i any port 7400
```

3. **性能监控**
```bash
# 查看程序资源使用
top -p $(pgrep -f mqtt_sender)
```

## 🎯 项目特色

1. **模块化设计**: 清晰的库与应用分离
2. **双向桥接**: 支持两个方向的消息转换
3. **类型安全**: 基于IDL的强类型检查
4. **高性能**: 支持100Hz+高频消息传输
5. **灵活配置**: 支持自定义频率、时长、内容
6. **完整日志**: 结构化日志便于调试
7. **易于扩展**: 简单添加新的消息类型

## 📚 学习资源

- **项目学习指南**: `test/MQTT_SENDER_学习文档.md`
- **FastDDS官方文档**: https://fast-dds.docs.eprosima.com/
- **MQTT协议规范**: https://mqtt.org/
- **演示脚本**: `test/demo_direction1.sh`, `test/test_direction2.sh`

## 🤝 贡献指南

1. Fork本项目
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启Pull Request

---

**项目维护**: 本项目实现了工业级的FastDDS与MQTT双向桥接通信系统，适用于车载系统、物联网设备等需要高性能本地通信与远程云端通信结合的场景。
