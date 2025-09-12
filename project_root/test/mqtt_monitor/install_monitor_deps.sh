#!/bin/bash
# MQTT Excel监控器依赖安装脚本

echo "=== 安装MQTT Excel监控器依赖 ==="

# 检查Python版本
python3 --version
if [ $? -ne 0 ]; then
    echo "[ERROR] Python3未安装，请先安装Python3"
    exit 1
fi

# 检查pip
pip3 --version
if [ $? -ne 0 ]; then
    echo "[ERROR] pip3未安装，请先安装pip3"
    exit 1
fi

echo "正在安装依赖包..."

# 安装MQTT客户端
pip3 install paho-mqtt

# 安装Excel操作库
pip3 install openpyxl

echo ""
echo "[SUCCESS] 依赖安装完成！"
echo ""
echo "现在可以运行监控器："
echo "python3 mqtt_excel_monitor.py"
echo ""
echo "监控器特性："
echo "- 每个MQTT话题对应一个Excel Sheet页"
echo "- 只记录数据变化，不记录重复值"
echo "- 实时保存到Excel文件"
echo "- 支持多话题同时监控"
echo "- 按Ctrl+C优雅停止"
