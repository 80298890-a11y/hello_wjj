#!/bin/bash

echo "=== J6下行系统启动脚本 ==="
echo

# 检查是否已构建
if [ ! -d "build" ] || [ ! -f "build/cloud_mqtt_sender" ]; then
    echo "❌ 系统未构建，请先运行: ./build.sh"
    exit 1
fi

# 配置文件路径
CONFIG_FILE="../config/downlink_config.json"
if [ ! -f "$CONFIG_FILE" ]; then
    CONFIG_FILE="/home/wjj/work/project_root/j6/downlink/config/downlink_config.json"
    if [ ! -f "$CONFIG_FILE" ]; then
        echo "❌ 配置文件未找到"
        exit 1
    fi
fi

echo "使用配置文件: $CONFIG_FILE"
echo

# 进入build目录
cd build

# 启动方式选择
echo "请选择启动方式:"
echo "1. 启动MQTT到FastDDS桥接器"
echo "2. 启动车机接收器"
echo "3. 启动云端MQTT发送器"
echo "4. 启动完整系统 (推荐测试顺序)"
echo "5. 退出"

read -p "请输入选择 (1-5): " choice

case $choice in
    1)
        echo "启动MQTT到FastDDS桥接器..."
        ./mqtt_to_fastdds_bridge "$CONFIG_FILE"
        ;;
    2)
        echo "启动车机接收器..."
        ./vehicle_receiver "$CONFIG_FILE"
        ;;
    3)
        echo "启动云端MQTT发送器..."
        ./cloud_mqtt_sender "$CONFIG_FILE"
        ;;
    4)
        echo "启动完整系统..."
        echo
        echo "推荐启动顺序:"
        echo "1. 先启动车机接收器"
        echo "2. 再启动MQTT到FastDDS桥接器"
        echo "3. 最后启动云端MQTT发送器"
        echo
        echo "按任意键启动车机接收器..."
        read
        gnome-terminal -- bash -c "./vehicle_receiver '$CONFIG_FILE'; exec bash" &
        
        echo "等待2秒后启动桥接器..."
        sleep 2
        gnome-terminal -- bash -c "./mqtt_to_fastdds_bridge '$CONFIG_FILE'; exec bash" &
        
        echo "等待2秒后启动云端发送器..."
        sleep 2
        gnome-terminal -- bash -c "./cloud_mqtt_sender '$CONFIG_FILE'; exec bash" &
        
        echo "✅ 完整系统已启动在不同终端中"
        echo "按 Ctrl+C 退出当前脚本"
        while true; do
            sleep 1
        done
        ;;
    5)
        echo "退出"
        exit 0
        ;;
    *)
        echo "❌ 无效选择"
        exit 1
        ;;
esac
