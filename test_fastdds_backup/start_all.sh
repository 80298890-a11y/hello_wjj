#!/bin/bash

# FastDDS 测试程序一键启动脚本
# 功能：启动所有发布者和订阅者程序

SCRIPT_DIR=$(dirname "$0")
BUILD_DIR="${SCRIPT_DIR}/build"
PID_DIR="${SCRIPT_DIR}/pids"

# 检查构建目录是否存在
if [ ! -d "${BUILD_DIR}" ]; then
    echo "❌ 构建目录不存在，请先运行 ./build.sh"
    exit 1
fi

# 创建PID存储目录
PID_DIR="$(realpath "${SCRIPT_DIR}")/pids"
mkdir -p "${PID_DIR}"

# 创建时间戳命名的日志目录
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_DIR="$(realpath "${SCRIPT_DIR}")/logs/${TIMESTAMP}"
mkdir -p "${LOG_DIR}"

echo "🚀 启动FastDDS测试程序..."
echo "📁 日志目录：${LOG_DIR}"

# 检查可执行文件是否存在
EXECUTABLES=(
    "handshake_publisher"
    "handshake_subscriber"
    "vehicle_status_publisher"
    "vehicle_status_subscriber" 
    "remote_control_publisher"
    "remote_control_subscriber"
)

for exe in "${EXECUTABLES[@]}"; do
    if [ ! -x "${BUILD_DIR}/${exe}" ]; then
        echo "❌ 可执行文件 ${exe} 不存在或无执行权限"
        exit 1
    fi
done

# 启动发布者程序
echo "📤 启动发布者程序..."

cd "${BUILD_DIR}" || { echo "无法进入构建目录"; exit 1; }

echo "  - 启动 handshake_publisher (100Hz)"
nohup ./handshake_publisher > "${LOG_DIR}/handshake_publisher.log" 2>&1 &
echo $! > "${PID_DIR}/handshake_publisher.pid"

echo "  - 启动 vehicle_status_publisher (100Hz)"  
nohup ./vehicle_status_publisher > "${LOG_DIR}/vehicle_status_publisher.log" 2>&1 &
echo $! > "${PID_DIR}/vehicle_status_publisher.pid"

echo "  - 启动 remote_control_publisher (100Hz)"
nohup ./remote_control_publisher > "${LOG_DIR}/remote_control_publisher.log" 2>&1 &
echo $! > "${PID_DIR}/remote_control_publisher.pid"

# 等待发布者启动
sleep 2

# 启动订阅者程序  
echo "📥 启动订阅者程序..."

echo "  - 启动 handshake_subscriber"
nohup ./handshake_subscriber > "${LOG_DIR}/handshake_subscriber.log" 2>&1 &
echo $! > "${PID_DIR}/handshake_subscriber.pid"

echo "  - 启动 vehicle_status_subscriber"
nohup ./vehicle_status_subscriber > "${LOG_DIR}/vehicle_status_subscriber.log" 2>&1 &
echo $! > "${PID_DIR}/vehicle_status_subscriber.pid"

echo "  - 启动 remote_control_subscriber"
nohup ./remote_control_subscriber > "${LOG_DIR}/remote_control_subscriber.log" 2>&1 &
echo $! > "${PID_DIR}/remote_control_subscriber.pid"

echo ""
echo "✅ 所有程序启动完成！"
echo ""
echo "📊 进程状态："
for exe in "${EXECUTABLES[@]}"; do
    if [ -f "${PID_DIR}/${exe}.pid" ]; then
        pid=$(cat "${PID_DIR}/${exe}.pid")
        if ps -p $pid > /dev/null 2>&1; then
            echo "  ✓ ${exe} (PID: $pid) - 运行中"
        else
            echo "  ❌ ${exe} - 启动失败"
        fi
    fi
done

echo ""
echo "📄 日志文件位置：${LOG_DIR}/"
echo "  handshake_publisher.log"
echo "  handshake_subscriber.log"
echo "  vehicle_status_publisher.log"
echo "  vehicle_status_subscriber.log"
echo "  remote_control_publisher.log"
echo "  remote_control_subscriber.log"

echo ""
echo "🛑 停止所有程序请运行: ./stop_all.sh"
echo "📊 查看运行状态请运行: ./status.sh"
