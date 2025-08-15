#!/bin/bash

# 定义项目根目录
PROJECT_ROOT=$(dirname "$0")
BUILD_DIR="${PROJECT_ROOT}/build"

echo "开始构建FastDDS测试程序..."

# 创建构建目录
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}" || { echo "无法进入构建目录"; exit 1; }

echo "运行CMake配置..."
cmake ..
if [ $? -ne 0 ]; then
    echo "CMake配置失败!"
    exit 1
fi

echo "编译所有程序..."
make -j$(nproc)
if [ $? -ne 0 ]; then
    echo "编译失败!"
    exit 1
fi

echo "构建完成。可执行文件在 ${BUILD_DIR} 目录下。"
echo ""
echo "可用的程序："
echo "  handshake_publisher      - 握手消息发布者"
echo "  handshake_subscriber     - 握手消息订阅者" 
echo "  vehicle_status_publisher - 车辆状态发布者"
echo "  vehicle_status_subscriber- 车辆状态订阅者"
echo "  remote_control_publisher - 远程控制发布者"
echo "  remote_control_subscriber- 远程控制订阅者"
echo ""
echo "运行示例："
echo "  ./handshake_publisher &"
echo "  ./handshake_subscriber"
