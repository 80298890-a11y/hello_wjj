#!/bin/bash

echo "=== control_cmd日志转CSV转换工具 ==="
echo ""

# 检查参数
if [ $# -eq 0 ]; then
    echo "用法: $0 <日志文件路径> [输出CSV文件路径]"
    echo ""
    echo "示例:"
    echo "  $0 /path/to/control_cmd.log"
    echo "  $0 /path/to/control_cmd.log output.csv"
    echo ""
    echo "当前可用的日志文件:"
    find /home/hello/work/project_root/test/build/logs -name "control_cmd.log" 2>/dev/null | head -5
    exit 1
fi

LOG_FILE="$1"
OUTPUT_FILE="$2"

# 检查日志文件是否存在
if [ ! -f "$LOG_FILE" ]; then
    echo "❌ 错误: 日志文件不存在: $LOG_FILE"
    exit 1
fi

# 进入脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 检查Python脚本是否存在
if [ ! -f "control_cmd_csv_converter.py" ]; then
    echo "❌ 错误: Python转换脚本不存在"
    exit 1
fi

# 执行转换
echo "🚀 开始转换..."
echo ""

if [ -n "$OUTPUT_FILE" ]; then
    python3 control_cmd_csv_converter.py "$LOG_FILE" -o "$OUTPUT_FILE"
else
    python3 control_cmd_csv_converter.py "$LOG_FILE"
fi

echo ""
echo "📁 转换完成的CSV文件保存在: $SCRIPT_DIR"
echo "💡 提示: 可以用Excel或LibreOffice Calc打开CSV文件进行分析"




