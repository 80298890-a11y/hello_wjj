# 云车互联通信系统 - 测试程序

本目录包含云车互联通信系统的核心程序，实现从云端到车端的多级MQTT转发和FastDDS数据分发。

## 🏗️ 系统架构

```
云端命令 → MQTT多级转发 → FastDDS发布 → 车端接收
```

### 数据流向

1. **云端发送**: `tsp/command/{vid}` → **tsp_forwarder**
2. **第一级转发**: `cloud/noa/{vid}` → **mqtt_simulator** 
3. **第二级转发**: `zhan/send/{vid}` → **data_processor**
4. **状态集成**: `/handshake/request/noa` → **bridge_forwarder**
5. **数据聚合**: 4个IDL话题 → **mqtt_to_fastdds_bridge**
6. **FastDDS发布**: 实时数据分发 → **车端接收**

## 📋 核心程序

### MQTT转发链路

| 程序名 | 功能描述 | 输入话题 | 输出话题 |
|--------|----------|----------|----------|
| `tsp_forwarder` | TSP命令转发器(MQTT程序1) | `tsp/command/{vid}` | `cloud/noa/{vid}` |
| `mqtt_simulator` | 状态机模拟器(MQTT程序X) | `cloud/noa/{vid}` | `zhan/send/{vid}` |
| `data_processor` | 数据处理器(MQTT程序2) | `zhan/send/{vid}` | `/handshake/request/noa` |
| `bridge_forwarder` | 数据聚合转发器(MQTT程序4) | `/handshake/request/noa` | 4个IDL话题 |

### FastDDS桥接

| 程序名 | 功能描述 | 频率 | 备注 |
|--------|----------|------|------|
| `mqtt_to_fastdds_bridge` | MQTT→FastDDS桥接器 | - | 接收4个IDL话题并发布到FastDDS |
| `fastdds_to_mqtt_bridge` | FastDDS→MQTT桥接器 | - | 反向链路(待开发) |

### 测试和模拟

| 程序名 | 功能描述 | 备注 |
|--------|----------|------|
| `mqtt_sender` | 车端模拟器 | 模拟固定车端数据发送 |
| `publisher` | FastDDS发布测试 | 测试FastDDS发布功能 |
| `subscriber` | FastDDS订阅测试 | 测试FastDDS接收功能 |

## ⚡ 关键特性

### 动态VID支持
- 支持任意Vehicle ID (123, 456, 789等)
- 每个VID独立的数据流处理
- 通配符订阅: `tsp/command/+`, `cloud/noa/+`, `zhan/send/+`

### 高频数据聚合
- **bridge_forwarder**: 400Hz聚合发送(4个IDL × 100Hz)
- **新消息检测**: 区分服务器新消息vs默认值lastvalue
- **状态驱动**: `/handshake/request`有外部状态源，其他3个IDL使用默认值

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
./subscriber &

# 2. 启动MQTT→FastDDS桥接器
./mqtt_to_fastdds_bridge &

# 3. 启动数据聚合转发器(400Hz)
./bridge_forwarder &

# 4. 启动数据处理器
./data_processor &

# 5. 启动MQTT状态机模拟器
./mqtt_simulator &

# 6. 启动TSP转发器
./tsp_forwarder &
```

### 3. 发送测试命令
```bash
# 发送启动命令到VID 123
mosquitto_pub -h hellorobotaxi.cn -p 11883 -t "tsp/command/123" \
  -m '{"action":"start","module":"noa","timestamp":1755591299000}'

# 发送停止命令到VID 456  
mosquitto_pub -h hellorobotaxi.cn -p 11883 -t "tsp/command/456" \
  -m '{"action":"stop","module":"noa","timestamp":1755591299000}'
```

### 4. 验证数据流
```bash
# 监听最终状态话题
mosquitto_sub -h hellorobotaxi.cn -p 11883 -t "/handshake/request/noa" -v

# 检查FastDDS接收(应显示100Hz)
# subscriber程序的终端输出会显示接收频率
```

## 🛠️ 开发工具

### 一键停止所有进程
```bash
pkill -f "mqtt|fastdds|bridge|publisher|subscriber|tsp_forwarder|mqtt_simulator|data_processor|bridge_forwarder"
```

### 查看实时日志
```bash
# 查看特定程序的最新日志
tail -f logs/bridge_forwarder/$(ls -t logs/bridge_forwarder/*.log | head -1)
tail -f logs/data_processor/$(ls -t logs/data_processor/*.log | head -1)
```

### 调试指定VID
```bash
# 监听特定VID的整个链路
mosquitto_sub -h hellorobotaxi.cn -p 11883 -t "tsp/command/789" -v &
mosquitto_sub -h hellorobotaxi.cn -p 11883 -t "cloud/noa/789" -v &  
mosquitto_sub -h hellorobotaxi.cn -p 11883 -t "zhan/send/789" -v &
mosquitto_sub -h hellorobotaxi.cn -p 11883 -t "/handshake/request/noa" -v &
```

## 🎯 IDL话题映射

| FastDDS话题 | 频率 | 数据来源 | 更新机制 |
|-------------|------|----------|----------|
| `/handshake/request` | 100Hz | 外部状态(`/handshake/request/noa`) | 状态驱动 + lastvalue |
| `/handshake/response` | 100Hz | 默认值 | lastvalue机制 |
| `/vehicle/status` | 100Hz | 默认值 | lastvalue机制 |
| `/vehicle/control_cmd` | 100Hz | 默认值 | lastvalue机制 |

## 📊 性能监控

- **总发送频率**: 400Hz (4个IDL轮流发送)
- **单IDL频率**: 100Hz
- **MQTT服务器**: hellorobotaxi.cn:11883
- **FastDDS域**: 默认域ID

## 💡 注意事项

1. **程序启动顺序很重要**: 按照快速启动指南的顺序启动
2. **VID支持**: 系统自动支持任意数字VID，无需预配置
3. **状态机制**: `data_processor`接收到消息就更新，不管状态是否变化
4. **日志清理**: 系统自动保留最新日志，定期清理旧文件
5. **新消息检测**: `bridge_forwarder`区分服务器新消息和默认值持续发送

## 🔗 相关文档

- `启动流程说明.txt`: 详细的手动启动步骤
- `../README.md`: 项目整体架构说明
- `../fastdds_lib/README.md`: FastDDS核心库文档
