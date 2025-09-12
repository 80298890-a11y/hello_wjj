#!/bin/bash

# FastDDS 测试程序一键停止脚本
# 功能：强制停止所有发布者和订阅者程序

SCRIPT_DIR=$(dirname "$0")
PID_DIR="${SCRIPT_DIR}/pids"

# 程序列表
EXECUTABLES=(
    "handshake_publisher"
    "handshake_subscriber"
    "vehicle_status_publisher"
    "vehicle_status_subscriber"
    "remote_control_publisher"
    "remote_control_subscriber"
)

# 强制停止通过PID文件管理的进程
for exe in "${EXECUTABLES[@]}"; do
    PID_FILE="${PID_DIR}/${exe}.pid"
    
    if [ -f "${PID_FILE}" ]; then
        pid=$(cat "${PID_FILE}")
        if ps -p $pid > /dev/null 2>&1; then
            kill -9 $pid > /dev/null 2>&1
        fi
        rm -f "${PID_FILE}"
    fi
done

# 强制清理所有相关进程（无论是否有PID文件）
pkill -f 'handshake_publisher' > /dev/null 2>&1
pkill -f 'handshake_subscriber' > /dev/null 2>&1
pkill -f 'vehicle_status_publisher' > /dev/null 2>&1
pkill -f 'vehicle_status_subscriber' > /dev/null 2>&1
pkill -f 'remote_control_publisher' > /dev/null 2>&1
pkill -f 'remote_control_subscriber' > /dev/null 2>&1

# 等待进程完全结束
sleep 1

# 清理PID目录
if [ -d "${PID_DIR}" ] && [ -z "$(ls -A ${PID_DIR} 2>/dev/null)" ]; then
    rmdir "${PID_DIR}" > /dev/null 2>&1
fi

echo "✅ 所有FastDDS程序已停止"
