#!/bin/bash

echo "Fast DDS Hello World 示例演示"
echo "============================"
echo ""

# 切换到 build 目录
cd build

echo "1. 测试发送方和接收方"
echo "   打开两个终端窗口："
echo "   终端 1: ./hello_world publisher"
echo "   终端 2: ./hello_world subscriber"
echo ""

echo "2. 测试发送有限样本"
echo "   终端 1: ./hello_world publisher -s 10"
echo "   终端 2: ./hello_world subscriber -s 10"
echo ""

echo "3. 测试使用 waitset 的订阅者"
echo "   终端 1: ./hello_world publisher"
echo "   终端 2: ./hello_world subscriber --waitset"
echo ""

echo "可执行文件位置: $(pwd)/hello_world"
echo ""

echo "是否要运行一个简单的演示？(y/n)"
read -r answer

if [[ $answer == "y" || $answer == "Y" ]]; then
    echo "正在启动发送方..."
    echo "请在另一个终端中运行: cd $(pwd) && ./hello_world subscriber"
    echo "按 Ctrl+C 停止发送方"
    echo ""
    ./hello_world publisher
else
    echo "演示结束。您可以手动运行上述命令。"
fi 