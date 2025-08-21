# J6 车机通信模拟系统

## 项目概述
J6项目是一个基于FastRTPS 2.6.10的车机通信模拟系统，分为上行(uplink)和下行(downlink)两个模块。

## 目录结构
```
j6/
├── uplink/          # 上行模拟器 - 模拟车机发送消息
│   ├── src/         # 源代码文件
│   ├── include/     # 头文件和IDL生成的类型支持文件
│   ├── idl/         # IDL定义文件
│   ├── build/       # 编译输出目录
│   ├── CMakeLists.txt
│   ├── build.sh     # 构建脚本
│   └── run_simulator.sh  # 启动脚本
└── downlink/        # 下行模拟器 - 接收消息并记录日志 (待开发)
```

## 上行模拟器 (Uplink Simulator)

### 功能
- 模拟车机以100Hz频率发送消息
- 发送4种类型的消息：
  1. **handshake/request** - 握手请求 (HandshakeRequest)
  2. **handshake/response** - 握手响应 (HandshakeResponse)
  3. **vehicle/control_cmd** - 车辆控制命令 (ControlCmd)
  4. **vehicle/vehicle_status** - 车辆状态信息 (VehicleStatus)

### 快速开始
```bash
cd /home/wjj/work/project_root/j6/uplink
./run_simulator.sh
```

### 手动编译
```bash
cd /home/wjj/work/project_root/j6/uplink
./build.sh
```

### 手动运行
```bash
cd build
export LD_LIBRARY_PATH=/usr/local/fastdds-2.6.10/lib:/usr/local/lib:$LD_LIBRARY_PATH
./vehicle_uplink_simulator
```

## 技术规格

### 环境要求
- FastRTPS 2.6.10
- CMake 3.16+
- C++17编译器
- Linux系统

### 消息规格
- **发送频率**: 100Hz (每10ms发送一轮)
- **传输协议**: FastRTPS over UDP
- **服务质量**: RELIABLE_RELIABILITY_QOS
- **历史策略**: KEEP_LAST_HISTORY_QOS (深度10)

### Topic名称
- `handshake/request` - 握手请求
- `handshake/response` - 握手响应
- `vehicle/control_cmd` - 控制命令
- `vehicle/vehicle_status` - 车辆状态

## 下行模拟器 (Downlink Simulator)
待开发 - 将实现消息接收和日志落盘功能

## 开发说明

### IDL文件生成
如需重新生成IDL类型支持文件：
```bash
cd include
/usr/local/fastdds-2.6.10/bin/fastddsgen -replace ../idl/VehicleStatus.idl
/usr/local/fastdds-2.6.10/bin/fastddsgen -replace ../idl/Handshake.idl
/usr/local/fastdds-2.6.10/bin/fastddsgen -replace ../idl/RemoteControl.idl
mv *.cxx ../src/
```

### 性能监控
程序运行时每1000轮消息打印一次状态信息，包含：
- 已发送消息轮数
- 运行时间
- 发送频率确认

## 故障排除

### 编译问题
1. 确保FastRTPS 2.6.10正确安装在 `/usr/local/fastdds-2.6.10/`
2. 检查CMake版本 >= 3.16
3. 确保有C++17编译器支持

### 运行时问题
1. 确保设置了正确的LD_LIBRARY_PATH
2. 检查防火墙设置是否允许UDP通信
3. 确认FastRTPS发现端口(7400)未被占用

