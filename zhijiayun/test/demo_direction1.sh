#!/bin/bash

# 方向1完整流程演示
# FastDDS发送 → FastDDS接收 → 转换为MQTT → MQTT接收

echo "🚀 方向1完整流程演示"
echo "FastDDS发送 → FastDDS接收 → 转换为MQTT → MQTT接收"
echo "============================================="

# 清理之前的进程
echo "🧹 清理之前的进程..."
pkill -f "fastdds_publisher_main|fastdds_subscriber_main|fastdds_to_mqtt_bridge|mqtt_receiver" 2>/dev/null || true

# 创建演示日志目录
DEMO_LOG_DIR="/tmp/demo_direction1_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$DEMO_LOG_DIR"
echo "📁 演示日志目录: $DEMO_LOG_DIR"

echo ""
echo "🔧 启动组件..."

# 1. 启动FastDDS接收器（验证）
echo "1️⃣ 启动FastDDS接收器..."
./subscriber > "$DEMO_LOG_DIR/fastdds_subscriber.log" 2>&1 &
SUBSCRIBER_PID=$!
echo "   PID: $SUBSCRIBER_PID"

# 2. 启动FastDDS→MQTT桥接器
echo "2️⃣ 启动FastDDS→MQTT桥接器..."
./fastdds_to_mqtt_bridge > "$DEMO_LOG_DIR/bridge.log" 2>&1 &
BRIDGE_PID=$!
echo "   PID: $BRIDGE_PID"

# 3. 启动MQTT接收器
echo "3️⃣ 启动MQTT接收器..."
./mqtt_receiver > "$DEMO_LOG_DIR/mqtt_receiver.log" 2>&1 &
MQTT_PID=$!
echo "   PID: $MQTT_PID"

# 等待组件启动
echo ""
echo "⏳ 等待组件启动完成 (8秒)..."
sleep 8

# 检查组件状态
echo ""
echo "📊 检查组件状态..."
if kill -0 $SUBSCRIBER_PID 2>/dev/null; then
    echo "✅ FastDDS接收器运行正常"
else
    echo "❌ FastDDS接收器启动失败"
fi

if kill -0 $BRIDGE_PID 2>/dev/null; then
    echo "✅ FastDDS→MQTT桥接器运行正常"
else
    echo "❌ FastDDS→MQTT桥接器启动失败"
fi

if kill -0 $MQTT_PID 2>/dev/null; then
    echo "✅ MQTT接收器运行正常"
else
    echo "❌ MQTT接收器启动失败"
fi

# 开始发送演示消息
echo ""
echo "🎯 开始发送演示消息..."
echo "流程: FastDDS发送 → FastDDS接收 → 转换为MQTT → MQTT接收"
echo ""

# 发送测试消息
test_messages=(
    "vehicle_status vehicle_id=11111 speed=60.5"
    "handshake_request noa_active=1 override_status=0"
    "remote_control steering_angle=10.5"
    "handshake_response noa_active=1 override_response=1"
    "vehicle_status vehicle_id=22222 speed=80.3"
)

for i in "${!test_messages[@]}"; do
    msg="${test_messages[$i]}"
    echo "📤 [$((i+1))/${#test_messages[@]}] 发送: $msg"
    
    # 发送消息
    ./publisher $msg > "$DEMO_LOG_DIR/publisher_$((i+1)).log" 2>&1
    
    # 等待处理
    sleep 2
    
    # 检查接收情况
    fastdds_count=$(grep -c "条消息" "$DEMO_LOG_DIR/fastdds_subscriber.log" 2>/dev/null || echo "0")
    bridge_count=$(grep -c "MQTT发布成功" "$DEMO_LOG_DIR/bridge.log" 2>/dev/null || echo "0")
    mqtt_count=$(find logs/mqtt/receiver/ -name "*.log" -exec cat {} \; 2>/dev/null | wc -l || echo "0")
    
    echo "   📨 FastDDS接收器收到: $fastdds_count 条"
    echo "   🌐 MQTT转发成功: $bridge_count 条"
    echo "   📱 MQTT接收到: $mqtt_count 条"
    echo ""
done

# 发送持续消息流
echo "🔄 发送持续消息流 (10秒)..."
./publisher > "$DEMO_LOG_DIR/publisher_continuous.log" 2>&1 &
PUBLISHER_PID=$!

# 监控10秒
for i in {1..10}; do
    sleep 1
    
    # 统计接收情况
    fastdds_count=$(grep -c "条消息" "$DEMO_LOG_DIR/fastdds_subscriber.log" 2>/dev/null || echo "0")
    bridge_count=$(grep -c "MQTT发布成功" "$DEMO_LOG_DIR/bridge.log" 2>/dev/null || echo "0")
    mqtt_count=$(find logs/mqtt/receiver/ -name "*.log" -exec cat {} \; 2>/dev/null | wc -l || echo "0")
    
    echo "⏰ ${i}s - FastDDS:$fastdds_count | Bridge:$bridge_count | MQTT:$mqtt_count"
done

# 停止发送
echo ""
echo "🛑 停止持续发送..."
kill $PUBLISHER_PID 2>/dev/null || true
wait $PUBLISHER_PID 2>/dev/null || true

# 等待处理完成
sleep 3

# 最终统计
echo ""
echo "📊 最终统计..."
fastdds_count=$(grep -c "条消息" "$DEMO_LOG_DIR/fastdds_subscriber.log" 2>/dev/null || echo "0")
bridge_count=$(grep -c "MQTT发布成功" "$DEMO_LOG_DIR/bridge.log" 2>/dev/null || echo "0")
mqtt_count=$(find logs/mqtt/receiver/ -name "*.log" -exec cat {} \; 2>/dev/null | wc -l || echo "0")

echo "==============================================="
echo "🎉 演示完成！最终统计:"
echo "   📨 FastDDS接收器接收: $fastdds_count 条消息"
echo "   🔄 Bridge转发成功: $bridge_count 条消息"
echo "   📱 MQTT服务器接收: $mqtt_count 条消息"
echo ""
echo "📁 详细日志位于: $DEMO_LOG_DIR"
echo "   - fastdds_subscriber.log  (FastDDS接收日志)"
echo "   - bridge.log              (桥接转发日志)"
echo "   - mqtt_receiver.log       (MQTT接收日志)"
echo "   - publisher_*.log         (发送日志)"
echo ""

# 清理进程
echo "🧹 清理演示进程..."
kill $SUBSCRIBER_PID $BRIDGE_PID $MQTT_PID 2>/dev/null || true
wait $SUBSCRIBER_PID $BRIDGE_PID $MQTT_PID 2>/dev/null || true

echo ""
if [ "$bridge_count" -gt 0 ] && [ "$mqtt_count" -gt 0 ]; then
    echo "✅ 方向1流程验证成功！"
    echo "   FastDDS → MQTT 转换正常工作"
else
    echo "❌ 方向1流程存在问题："
    if [ "$fastdds_count" -eq 0 ]; then
        echo "   - FastDDS消息发送/接收失败"
    elif [ "$bridge_count" -eq 0 ]; then
        echo "   - FastDDS→MQTT桥接器转换失败"
    elif [ "$mqtt_count" -eq 0 ]; then
        echo "   - MQTT消息接收失败"
    fi
    echo "   请检查日志文件: $DEMO_LOG_DIR"
fi

echo ""
echo "📖 如需查看实时日志："
echo "   tail -f $DEMO_LOG_DIR/*.log"
