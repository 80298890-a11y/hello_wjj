#!/bin/bash

# 确保脚本在错误时退出
set -e

# 检查必要的头文件和库
echo "检查依赖项..."

# 检查jsoncpp
if [ ! -f "/usr/include/json/json.h" ] && [ ! -f "/usr/include/jsoncpp/json/json.h" ] && [ ! -f "/usr/local/include/json/json.h" ] && [ ! -f "/usr/include/aarch64-linux-gnu/json/json.h" ]; then
    echo "错误: 未找到 jsoncpp 头文件"
    echo "请安装 libjsoncpp-dev"
    exit 1
fi

# 检查mosquitto
if [ ! -f "/usr/include/mosquitto.h" ] && [ ! -f "/usr/local/include/mosquitto.h" ]; then
    echo "错误: 未找到 mosquitto 头文件"
    echo "请安装 libmosquitto-dev"
    exit 1
fi

# 创建并进入build目录
mkdir -p build
cd build

# 运行CMake
echo "运行 CMake..."
cmake ..

# 编译
echo "编译..."
make -j$(nproc)

echo "编译完成"