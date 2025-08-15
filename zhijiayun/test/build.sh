#!/bin/bash

# 统一构建脚本 - 方向1专用
echo "🔨 方向1构建脚本"
echo "FastDDS发送 → FastDDS接收 → 转换为MQTT → MQTT接收"
echo "=============================================="

# 设置错误时退出
set -e

# 项目根目录
PROJECT_ROOT=$(cd "$(dirname "$0")/.." && pwd)
echo "项目根目录: $PROJECT_ROOT"

# 1. 编译FastDDS库
echo ""
echo "1️⃣ 编译FastDDS基础库..."
cd "$PROJECT_ROOT/fastdds_lib"

# 创建build目录
mkdir -p build
cd build

# CMake配置和编译
echo "   配置CMake..."
cmake .. > /dev/null 2>&1

echo "   编译FastDDS库..."
make fastdds_lib > /dev/null 2>&1

echo "   ✅ FastDDS基础库编译完成"

# 2. 编译test目录中的业务程序
echo ""
echo "2️⃣ 编译业务逻辑程序..."
cd "$PROJECT_ROOT/test"

# 创建build目录
mkdir -p build
cd build

# CMake配置和编译
echo "   配置CMake..."
cmake .. > /dev/null 2>&1

echo "   编译publisher (FastDDS发送逻辑)..."
make publisher > /dev/null 2>&1

echo "   编译subscriber (FastDDS接收逻辑)..."
make subscriber > /dev/null 2>&1

echo "   编译fastdds_to_mqtt_bridge (转换桥接逻辑)..."
make fastdds_to_mqtt_bridge > /dev/null 2>&1

cd ..

# 编译mqtt_receiver（直接用g++）
echo "   编译mqtt_receiver (MQTT接收逻辑)..."
g++ -std=c++17 -O2 \
    mqtt_receiver.cpp \
    -lpthread \
    -o mqtt_receiver > /dev/null 2>&1
    
echo "   编译mqtt_sender (MQTT发送逻辑)..."
# 删除旧的可执行文件确保重新编译
rm -f mqtt_sender
# 重新编译（添加mosquitto库）
g++ -std=c++17 -O2 \
    mqtt_sender.cpp \
    -L/usr/lib/aarch64-linux-gnu \
    -lmosquitto \
    -lpthread \
    -o mqtt_sender > /dev/null 2>&1

echo "   ✅ 业务逻辑程序编译完成"



# 3. 复制可执行文件到test目录
echo ""
echo "3️⃣ 复制可执行文件..."

cp build/publisher . 2>/dev/null || echo "   警告: publisher复制失败"
cp build/subscriber . 2>/dev/null || echo "   警告: subscriber复制失败"  
cp build/fastdds_to_mqtt_bridge . 2>/dev/null || echo "   警告: fastdds_to_mqtt_bridge复制失败"

echo "   ✅ 可执行文件复制完成"

# 4. 验证编译结果
echo ""
echo "4️⃣ 验证编译结果..."

PROGRAMS=(
    "publisher"
    "subscriber"
    "fastdds_to_mqtt_bridge"
    "mqtt_receiver"
    "mqtt_sender"
)

ALL_SUCCESS=true
for prog in "${PROGRAMS[@]}"; do
    if [ -x "$prog" ]; then
        echo "   ✅ $prog"
    else
        echo "   ❌ $prog (缺失或不可执行)"
        ALL_SUCCESS=false
    fi
done

echo ""
if [ "$ALL_SUCCESS" = true ]; then
    echo "🎉 方向1所有组件编译成功！"
    echo ""
    echo "📚 业务逻辑程序："
    echo "   publisher               - FastDDS发送逻辑程序"
    echo "   subscriber              - FastDDS接收逻辑程序" 
    echo "   fastdds_to_mqtt_bridge  - FastDDS转MQTT桥接逻辑"
    echo "   mqtt_receiver           - MQTT接收逻辑程序"
    echo "   mqtt_sender             - MQTT发送逻辑程序（方向2）"
    echo ""
    echo "🔧 基础库位置："
    echo "   FastDDS基础库: ../fastdds_lib/build/libfastdds_lib.a"
    echo ""
    echo "🚀 快速验证："
    echo "   ./demo_direction1.sh   - 完整方向1流程演示"
    echo ""
    echo "📖 详细说明请参考 README.md"
else
    echo "❌ 部分组件编译失败，请检查错误信息"
    exit 1
fi