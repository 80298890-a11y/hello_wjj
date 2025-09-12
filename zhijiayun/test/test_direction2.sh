#!/bin/bash

# Direction 2 测试脚本：MQTT → FastDDS 完整流程测试
# 使用方法：./test_direction2.sh

echo "======================================"
echo "Direction 2 完整流程测试"
echo "MQTT → FastDDS 转换验证"
echo "======================================"

# 清理之前的日志
echo "清理旧日志..."
rm -rf logs/mqtt/send/* logs/fastdds/sub/* logs/fastdds_mqtt/mqtt_to_fastdds/* 2>/dev/null

echo
echo "1. 启动FastDDS订阅器 (后台运行)"
./subscriber &
SUBSCRIBER_PID=$!
echo "   订阅器PID: $SUBSCRIBER_PID"
sleep 2

echo
echo "2. 启动MQTT到FastDDS桥接器 (后台运行)"
./mqtt_to_fastdds_bridge &
BRIDGE_PID=$!
echo "   桥接器PID: $BRIDGE_PID"

# 等待桥接器完全初始化
echo "   等待桥接器初始化完成..."
sleep 5
echo "   ✅ 桥接器应该已准备就绪"

echo
echo "3. 发送MQTT测试消息"
echo "   发送连续3帧消息 (每个topic 100Hz)..."
./mqtt_sender

echo
echo "4. 等待消息处理完成..."
sleep 3

echo
echo "5. 停止所有程序"
kill $BRIDGE_PID 2>/dev/null
kill $SUBSCRIBER_PID 2>/dev/null
sleep 1

echo
echo "======================================"
echo "测试结果检查"
echo "======================================"

# 检查MQTT发送日志
echo
echo "📤 MQTT发送结果："
if [ -d "logs/mqtt/send" ]; then
    latest_send_dir=$(ls -t logs/mqtt/send/ | head -1)
    if [ -n "$latest_send_dir" ]; then
        echo "   日志目录: logs/mqtt/send/$latest_send_dir"
        for topic_dir in logs/mqtt/send/$latest_send_dir/*/; do
            if [ -d "$topic_dir" ]; then
                topic_name=$(basename "$topic_dir")
                log_file="$topic_dir$topic_name.log"
                if [ -f "$log_file" ]; then
                    count=$(wc -l < "$log_file")
                    echo "   ✅ $topic_name: $count 条消息发送"
                fi
            fi
        done
    else
        echo "   ❌ 未找到发送日志"
    fi
else
    echo "   ❌ 发送日志目录不存在"
fi

# 检查FastDDS接收日志
echo
echo "📥 FastDDS接收结果："
if [ -d "logs/fastdds/sub" ]; then
    latest_sub_dir=$(ls -t logs/fastdds/sub/ | head -1)
    if [ -n "$latest_sub_dir" ]; then
        echo "   日志目录: logs/fastdds/sub/$latest_sub_dir"
        for topic_dir in logs/fastdds/sub/$latest_sub_dir/*/; do
            if [ -d "$topic_dir" ]; then
                topic_name=$(basename "$topic_dir")
                log_file="$topic_dir$topic_name.log"
                if [ -f "$log_file" ]; then
                    count=$(wc -l < "$log_file")
                    echo "   ✅ $topic_name: $count 条消息接收"
                fi
            fi
        done
    else
        echo "   ❌ 未找到接收日志"
    fi
else
    echo "   ❌ 接收日志目录不存在"
fi

# 检查桥接器转换日志
echo
echo "🌉 桥接器转换结果："
if [ -d "logs/fastdds_mqtt/mqtt_to_fastdds" ]; then
    latest_bridge_dir=$(ls -t logs/fastdds_mqtt/mqtt_to_fastdds/ | head -1)
    if [ -n "$latest_bridge_dir" ]; then
        echo "   日志目录: logs/fastdds_mqtt/mqtt_to_fastdds/$latest_bridge_dir"
        for topic_dir in logs/fastdds_mqtt/mqtt_to_fastdds/$latest_bridge_dir/*/; do
            if [ -d "$topic_dir" ]; then
                topic_name=$(basename "$topic_dir")
                log_file="$topic_dir$topic_name.log"
                if [ -f "$log_file" ]; then
                    count=$(wc -l < "$log_file")
                    echo "   ✅ $topic_name: $count 条消息转换"
                fi
            fi
        done
    else
        echo "   ❌ 未找到转换日志"
    fi
else
    echo "   ❌ 转换日志目录不存在"
fi

echo
echo "======================================"
echo "Direction 2 测试完成"
echo "======================================"

# 清理后台进程
pkill -f subscriber 2>/dev/null
pkill -f mqtt_to_fastdds_bridge 2>/dev/null
