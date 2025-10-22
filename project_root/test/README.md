# 云车互联通信系统 - 测试程序

本目录包含云车互联通信系统的核心程序，实现MQTT桥接转发和FastDDS数据分发。

## 🏗️ 系统架构

```
外部MQTT(带VID) → MQTT桥接转发 → 本地MQTT(无VID) → FastDDS发布 → 车端接收
```

### 数据流向

1. **MQTT桥接**: 外部MQTT服务器带VID话题 → **bridge_forwarder** → 本地MQTT服务器无VID话题
2. **FastDDS转换**: 本地MQTT话题 → **mqtt_to_fastdds_bridge** → FastDDS话题
3. **数据分发**: FastDDS话题 → **车端接收**

## 📋 核心程序

### 核心桥接程序

| 程序名 | 功能描述 | 输入 | 输出 |
|--------|----------|------|------|
| `bridge_forwarder` | MQTT桥接转发器(单向) | 外部MQTT:`/handshake/request/{vid}`, `/vehicle/control_cmd/{vid}` | 本地MQTT:`/handshake/request`, `/vehicle/control_cmd` |
| `mqtt_to_fastdds_bridge` | MQTT→FastDDS桥接器 | 本地MQTT话题 | FastDDS话题 |
| `fastdds_to_mqtt_bridge` | FastDDS→MQTT桥接器 | FastDDS话题 | MQTT话题 |
| `tsp_command_processor` | TSP命令处理器 | MQTT命令 | 处理结果 |

### 测试和工具程序

| 程序名 | 功能描述 | 备注 |
|--------|----------|------|
| `mqtt_sender` | MQTT发送测试工具 | 模拟MQTT消息发送 |
| `mqtt_receiver` | MQTT接收测试工具 | 监听和接收MQTT消息 |
| `publisher` | FastDDS发布测试 | 测试FastDDS发布功能 |
| `subscriber` | FastDDS订阅测试 | 测试FastDDS接收功能 |
| `python_bridge_forwarder.py` | Python版本桥接器 | bridge_forwarder的Python实现 |

### 车辆指令程序

| 程序名 | 功能描述 |
|--------|----------|
| `vehicle_instruction_braking` | 车辆制动指令处理 |
| `vehicle_instruction_distance_time` | 车辆距离时间指令处理 |
| `vehicle_instruction_lateral` | 车辆横向指令处理 |
| `vehicle_instruction_takeover` | 车辆接管指令处理 |

## ⚡ 关键特性

### 动态VID支持
- 支持任意Vehicle ID (123, 456, 789等)
- 自动识别并处理任何VID的消息
- 通配符订阅: `/handshake/request/+`, `/vehicle/control_cmd/+`

### MQTT桥接转发
- **bridge_forwarder**: 50Hz单向桥接转发
- **双MQTT连接**: 外部服务器(hellorobotaxi.cn:11883) → 本地服务器(127.0.0.1:1883)
- **VID处理**: 自动去除消息话题中的车辆ID后缀
- **重复消息过滤**: 智能过滤重复消息内容，避免无效转发
- **支持话题**: `/handshake/request/{vid}` 和 `/vehicle/control_cmd/{vid}`

### 多级时间戳
- `timestamp`: 云端原始时间戳
- `mqtt_send_timestamp`: MQTT程序1添加
- `simulator_timestamp`: MQTT程序X添加

### 异步日志系统
- 程序特定子目录: `logs/{program_name}/`
- 时间戳文件名: `YYYYMMDD_HHMMSS_mmm.log`
- 简化终端输出，详细磁盘日志

## 🚀 快速启动

### 1. 编译所有程序
```bash
cd build
make -j$(nproc)
cd ..
```

### 2. 启动核心链路(按顺序)
```bash
# 1. 启动FastDDS订阅端(测试用)
./build/subscriber &

# 2. 启动MQTT→FastDDS桥接器
./build/mqtt_to_fastdds_bridge &

# 3. 启动MQTT桥接转发器(50Hz)
./build/bridge_forwarder &

# 可选: 启动其他测试工具
./build/mqtt_receiver &  # MQTT接收监控
./build/publisher &      # FastDDS发布测试
```

### 3. 发送测试命令
```bash
# 发送握手请求到VID 123
mosquitto_pub -h hellorobotaxi.cn -p 11883 -t "/handshake/request/123" \
  -m '{"noa_active_request":1.0,"remote_override_status":0.0,"remote_override_ready":1.0,"timestamp":1755591299000}'

# 发送车辆控制命令到VID 456  
mosquitto_pub -h hellorobotaxi.cn -p 11883 -t "/vehicle/control_cmd/456" \
  -m '{"steering_angle":15.5,"target_acceleration":2.0,"gear_position":1.0,"timestamp":1755591299000}'
```

### 4. 验证数据流
```bash
# 监听外部MQTT输入(带VID)
mosquitto_sub -h hellorobotaxi.cn -p 11883 -t "/handshake/request/+" -v
mosquitto_sub -h hellorobotaxi.cn -p 11883 -t "/vehicle/control_cmd/+" -v

# 监听本地MQTT输出(无VID) 
mosquitto_sub -h 127.0.0.1 -p 1883 -t "/handshake/request" -v
mosquitto_sub -h 127.0.0.1 -p 1883 -t "/vehicle/control_cmd" -v

# 检查FastDDS接收
# subscriber程序的终端输出会显示接收频率
```

## 🛠️ 开发工具

### 一键停止所有进程
```bash
pkill -f "mqtt|fastdds|bridge|publisher|subscriber|tsp_command_processor"
```

### 查看实时日志
```bash
# 查看特定程序的最新日志
tail -f logs/bridge_forwarder/$(ls -t logs/bridge_forwarder/*.log | head -1)

# 查看所有可用日志目录
ls -la logs/
```

### 调试指定VID
```bash
# 监听特定VID的桥接链路
mosquitto_sub -h hellorobotaxi.cn -p 11883 -t "/handshake/request/789" -v &
mosquitto_sub -h hellorobotaxi.cn -p 11883 -t "/vehicle/control_cmd/789" -v &
mosquitto_sub -h 127.0.0.1 -p 1883 -t "/handshake/request" -v &
mosquitto_sub -h 127.0.0.1 -p 1883 -t "/vehicle/control_cmd" -v &
```

## 🎯 MQTT话题桥接映射

| 外部MQTT话题 | 本地MQTT话题 | 频率 | 处理方式 |
|-------------|-------------|------|----------|
| `/handshake/request/{vid}` | `/handshake/request` | 50Hz | VID去除 + JSON解析 |
| `/vehicle/control_cmd/{vid}` | `/vehicle/control_cmd` | 50Hz | VID去除 + JSON解析 |

## 🎯 FastDDS话题映射 (由mqtt_to_fastdds_bridge处理)

| FastDDS话题 | 频率 | 数据来源 | 更新机制 |
|-------------|------|----------|----------|
| `/handshake/request` | 按需 | 本地MQTT:`/handshake/request` | 消息驱动 |
| `/vehicle/control_cmd` | 按需 | 本地MQTT:`/vehicle/control_cmd` | 消息驱动 |

## 📊 性能监控

- **桥接转发频率**: 50Hz (bridge_forwarder)
- **支持话题数量**: 2个 (`/handshake/request`, `/vehicle/control_cmd`)
- **外部MQTT服务器**: hellorobotaxi.cn:11883
- **本地MQTT服务器**: 127.0.0.1:1883
- **FastDDS域**: 默认域ID

## 💡 注意事项

1. **程序启动顺序很重要**: 先启动订阅端，再启动桥接器，最后启动发布端
2. **VID支持**: 系统自动支持任意数字VID，无需预配置
3. **桥接机制**: `bridge_forwarder`实现外部→本地MQTT单向桥接，50Hz频率转发
4. **日志清理**: 系统自动保留最新日志，定期清理旧文件
5. **重复消息过滤**: 自动过滤重复内容，提高传输效率
