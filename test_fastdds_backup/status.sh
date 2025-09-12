#!/bin/bash

# FastDDS 测试程序状态检查脚本
# 功能：显示所有程序的运行状态

SCRIPT_DIR=$(dirname "$0")
PID_DIR="${SCRIPT_DIR}/pids"

echo "📊 FastDDS测试程序运行状态"
echo "================================"

# 程序列表
EXECUTABLES=(
    "handshake_publisher"
    "handshake_subscriber"
    "vehicle_status_publisher"
    "vehicle_status_subscriber"
    "remote_control_publisher"
    "remote_control_subscriber"
)

# 统计变量
running_count=0
total_count=${#EXECUTABLES[@]}

# 检查每个程序的状态
for exe in "${EXECUTABLES[@]}"; do
    PID_FILE="${PID_DIR}/${exe}.pid"
    
    printf "%-30s " "${exe}:"
    
    if [ -f "${PID_FILE}" ]; then
        pid=$(cat "${PID_FILE}")
        if ps -p $pid > /dev/null 2>&1; then
            # 获取进程的运行时间和CPU使用率
            ps_info=$(ps -p $pid -o pid,ppid,etime,%cpu --no-headers 2>/dev/null)
            if [ -n "$ps_info" ]; then
                etime=$(echo "$ps_info" | awk '{print $3}')
                cpu=$(echo "$ps_info" | awk '{print $4}')
                echo "✅ 运行中 (PID: $pid, 运行时间: $etime, CPU: ${cpu}%)"
                ((running_count++))
            else
                echo "❌ 进程信息获取失败"
            fi
        else
            echo "❌ 已停止 (PID文件存在但进程不存在)"
        fi
    else
        echo "⭕ 未启动 (无PID文件)"
    fi
done

echo ""
echo "================================"
echo "📈 总体状态: $running_count/$total_count 程序运行中"

if [ $running_count -eq $total_count ]; then
    echo "🎉 所有程序正常运行！"
elif [ $running_count -eq 0 ]; then
    echo "😴 所有程序已停止"
    echo "💡 启动程序请运行: ./start_all.sh"
else
    echo "⚠️  部分程序运行中，建议检查日志文件"
fi

# 显示系统资源使用情况
echo ""
echo "💻 系统资源使用情况:"
echo "内存使用率: $(free | grep Mem | awk '{printf("%.1f%%", $3/$2 * 100.0)}')"
echo "CPU负载: $(uptime | awk -F'load average:' '{print $2}' | sed 's/^[ \t]*//')"

# 显示日志文件信息
echo ""
echo "📄 日志文件状态:"
BASE_LOG_DIR="${SCRIPT_DIR}/logs"
if [ -d "${BASE_LOG_DIR}" ]; then
    # 找到最新的日志目录
    LATEST_LOG_DIR=$(find "${BASE_LOG_DIR}" -maxdepth 1 -type d -name "*_*" | sort | tail -1)
    if [ -n "$LATEST_LOG_DIR" ]; then
        echo "  📁 最新日志目录: $(basename "$LATEST_LOG_DIR")"
        for exe in "${EXECUTABLES[@]}"; do
            log_file="${LATEST_LOG_DIR}/${exe}.log"
            if [ -f "${log_file}" ]; then
                size=$(ls -lh "${log_file}" | awk '{print $5}')
                lines=$(wc -l < "${log_file}")
                printf "  %-30s 大小: %s, 行数: %s\n" "${exe}.log" "$size" "$lines"
            fi
        done
    else
        echo "  📁 没有找到时间戳命名的日志目录"
    fi
else
    echo "  📁 日志目录不存在"
fi

echo ""
echo "🛠️  可用命令:"
echo "  ./start_all.sh  - 启动所有程序"
echo "  ./stop_all.sh   - 停止所有程序"
echo "  ./status.sh     - 查看运行状态"
