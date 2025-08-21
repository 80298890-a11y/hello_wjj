#!/bin/bash

echo "=== J6 车机上行模拟器启动脚本 ==="
echo ""

# 设置库路径
export LD_LIBRARY_PATH=/usr/local/fastdds-2.6.10/lib:/usr/local/lib:$LD_LIBRARY_PATH

# 检查是否已编译
if [ ! -f "build/vehicle_uplink_simulator" ]; then
    echo "[INFO] 未找到可执行文件，正在编译..."
    ./build.sh
    if [ $? -ne 0 ]; then
        echo "[ERROR] 编译失败!"
        exit 1
    fi
fi

echo "启动车机上行模拟器..."
echo "发送消息类型:"
echo "  - handshake/request (HandshakeRequest)"
echo "  - handshake/response (HandshakeResponse)"  
echo "  - vehicle/control_cmd (ControlCmd)"
echo "  - vehicle/vehicle_status (VehicleStatus)"
echo ""
echo "发送频率: 100Hz"
echo "按Ctrl+C停止程序"
echo ""

cd build
./vehicle_uplink_simulator

