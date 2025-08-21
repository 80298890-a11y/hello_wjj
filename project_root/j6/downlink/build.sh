#!/bin/bash

echo "=== J6下行系统构建脚本 ==="
echo "FastDDS 2.6.10 + MQTT + 配置文件支持"
echo

# 检查必要的依赖
echo "检查依赖..."

# 检查FastDDS
if [ ! -d "/usr/local/fastdds-2.6.10" ]; then
    echo "❌ FastDDS 2.6.10 未找到"
    exit 1
fi

# 检查jsoncpp
if ! pkg-config --exists jsoncpp; then
    echo "❌ jsoncpp 未安装，正在安装..."
    sudo apt-get update
    sudo apt-get install -y libjsoncpp-dev
fi

# 检查mosquitto
if ! pkg-config --exists libmosquitto; then
    echo "❌ libmosquitto 未安装，正在安装..."
    sudo apt-get update  
    sudo apt-get install -y libmosquitto-dev
fi

echo "✅ 依赖检查完成"

# 创建build目录
if [ -d "build" ]; then
    echo "清理旧的构建目录..."
    rm -rf build/*
else
    mkdir -p build
fi

# 切换到build目录
cd build

echo "配置项目..."
cmake .. || { echo "❌ CMake配置失败"; exit 1; }

echo "开始构建..."
make -j$(nproc) || { echo "❌ 构建失败"; exit 1; }

echo
echo "✅ 构建完成!"
echo
echo "=== 可执行文件 ==="
echo "云端MQTT发送器: ./cloud_mqtt_sender [config_file]"
echo "MQTT到FastDDS桥接器: ./mqtt_to_fastdds_bridge [config_file]" 
echo "车机接收器: ./vehicle_receiver [config_file]"
echo
echo "=== 使用说明 ==="
echo "1. 编辑配置文件: ../config/downlink_config.json"
echo "2. 启动桥接器: ./mqtt_to_fastdds_bridge"
echo "3. 启动车机接收器: ./vehicle_receiver"
echo "4. 启动云端发送器: ./cloud_mqtt_sender"
echo
echo "默认配置文件路径: /home/wjj/work/project_root/j6/downlink/config/downlink_config.json"
