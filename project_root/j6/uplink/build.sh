#!/bin/bash

echo "=== J6 上行系统构建脚本 ==="
echo ""

# 设置库路径
export LD_LIBRARY_PATH=/usr/local/fastdds-2.6.10/lib:/usr/local/lib:$LD_LIBRARY_PATH

# 创建构建目录
mkdir -p build
cd build

echo "1. 配置CMake..."
cmake ..

if [ $? -ne 0 ]; then
    echo "[ERROR] CMake配置失败!"
    exit 1
fi

echo ""
echo "2. 编译项目..."
make

if [ $? -ne 0 ]; then
    echo "[ERROR] 编译失败!"
    exit 1
fi

echo ""
echo "[SUCCESS] 编译完成!"
echo ""
echo "运行命令:"
echo "  cd build"
echo "  export LD_LIBRARY_PATH=/usr/local/fastdds-2.6.10/lib:/usr/local/lib:\$LD_LIBRARY_PATH"
echo ""
echo "车机模拟器 (发送端):"
echo "  ./vehicle_uplink_simulator"
echo ""
echo "消息接收器 (接收端):"
echo "  ./j6_message_receiver"
echo ""
echo "程序功能:"
echo "  - 车机模拟器: 以100Hz频率发送消息"
echo "  - 消息接收器: 接收消息并异步记录日志"
echo "  - 支持4种消息类型: HandshakeRequest, HandshakeResponse, ControlCmd, VehicleStatus"
echo "  - 日志格式: logs/时间戳/topic名称.log"
