# FastDDS 测试程序

本项目实现了基于FastDDS的发布/订阅通信程序，支持三种消息类型：

- **HandShake** - 握手消息（请求和响应）
- **VehicleStatus** - 车辆状态信息  
- **RemoteControl** - 远程控制命令

## 📁 项目结构

```
project_root/test_fastdds/
├── build/                    # 构建输出目录
├── logs/                     # 程序运行日志
├── pids/                     # 进程PID文件（运行时生成）
├── *.idl                     # IDL接口定义文件
├── *_publisher.cpp           # 发布者程序源码
├── *_subscriber.cpp          # 订阅者程序源码
├── CMakeLists.txt            # CMake构建配置
├── build.sh                  # 构建脚本
├── start_all.sh              # 一键启动脚本
├── stop_all.sh               # 一键停止脚本
├── status.sh                 # 状态检查脚本
├── view_logs.sh              # 日志查看脚本
└── README.md                 # 本说明文件
```

## 🚀 快速开始

### 1. 编译程序

```bash
./build.sh
```

### 2. 启动所有程序

```bash
./start_all.sh
```

### 3. 查看运行状态

```bash
./status.sh
```

### 4. 停止所有程序

```bash
./stop_all.sh
```

## 📊 程序说明

### 发布者程序（100Hz频率）

- `handshake_publisher` - 发送握手请求和响应消息
- `vehicle_status_publisher` - 发送车辆状态信息
- `remote_control_publisher` - 发送远程控制命令

### 订阅者程序

- `handshake_subscriber` - 接收握手消息并显示
- `vehicle_status_subscriber` - 接收车辆状态并显示
- `remote_control_subscriber` - 接收控制命令并显示

## 🔧 手动运行单个程序

进入构建目录：
```bash
cd build/
```

启动发布者：
```bash
./handshake_publisher &
./vehicle_status_publisher &
./remote_control_publisher &
```

启动订阅者：
```bash
./handshake_subscriber &
./vehicle_status_subscriber &
./remote_control_subscriber &
```

## 📄 日志文件

程序运行日志按时间戳自动分类保存：

```
logs/
├── 20240807_091530/     # 2024年08月07日 09:15:30 启动的日志
│   ├── handshake_publisher.log
│   ├── handshake_subscriber.log
│   ├── vehicle_status_publisher.log
│   ├── vehicle_status_subscriber.log
│   ├── remote_control_publisher.log
│   └── remote_control_subscriber.log
├── 20240807_143022/     # 2024年08月07日 14:30:22 启动的日志
│   └── ...
└── ...
```

### 日志查看命令

```bash
# 查看所有日志目录
./view_logs.sh

# 查看指定批次的日志
./view_logs.sh 1

# 查看具体日志文件
./view_logs.sh 1 handshake_publisher.log

# 实时跟踪最新日志
./view_logs.sh tail
```

## 🎯 技术特性

- **精确时间控制**: 发布者使用100Hz精确频率（每10ms发送一次）
- **业务值映射**: HandShake消息支持原始业务值(-1,0,1)与IDL enum的自动映射
- **可靠传输**: 使用RELIABLE_RELIABILITY_QOS保证消息传递
- **模拟数据**: 发布者生成真实的模拟数据（正弦波、随机数等）

## 🛠️ 依赖要求

- CMake 3.12+
- FastDDS 2.10+
- C++17编译器
- fastddsgen工具

## 📝 IDL消息格式

### HandShake消息

```idl
struct HandshakeRequest {
    noa_active_request noa_active;      // 激活请求状态
    remote_override_status override_status;  // 覆盖状态
    remote_override_ready override_ready;    // 就绪状态
    unsigned long long timestamp;            // 时间戳
};

struct HandshakeResponse {
    noa_active_response noa_active;          // 激活响应
    RemoteOverrideResponse override_response; // 覆盖响应
    boolean current_control_source;          // 当前控制源
    unsigned long long timestamp;            // 时间戳
};
```

### VehicleStatus消息

```idl
struct VehicleStatus {
    unsigned long vehicle_id;        // 车辆ID
    control_mode current_control_mode; // 当前控制模式
    float position_longitude;        // 经度
    float position_latitude;         // 纬度
    float speed;                     // 速度
    // ... 更多车辆状态字段
};
```

### RemoteControl消息

```idl
struct ControlCmd {
    boolean steering_angle_enable;   // 转向角使能
    float steering_angle;            // 转向角
    boolean target_acceleration_enable; // 加速度使能
    float target_acceleration;       // 目标加速度
    // ... 更多控制字段
};
```

## 🔍 故障排除

### 编译失败

1. 检查FastDDS是否正确安装
2. 确保fastddsgen在系统PATH中
3. 检查C++17编译器支持

### 程序无法启动

1. 运行 `./status.sh` 检查状态
2. 查看对应的日志文件
3. 确保没有端口冲突

### 停止程序

- 正常停止：`./stop_all.sh`
- 强制停止：`pkill -f handshake; pkill -f vehicle_status; pkill -f remote_control`
