#!/bin/bash

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# 日志函数
log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# 检查程序是否在运行
check_running() {
    if pgrep -f "$1" > /dev/null; then
        return 0
    else
        return 1
    fi
}

# 停止现有程序
stop_programs() {
    log_info "停止现有程序..."
    
    if check_running "fastdds_subscriber"; then
        log_info "停止 FastDDS 订阅者..."
        pkill -f "fastdds_subscriber"
        sleep 1
    fi
    
    if check_running "fastdds_publisher"; then
        log_info "停止 FastDDS 发布者..."
        pkill -f "fastdds_publisher"
        sleep 1
    fi
}

# 编译程序
compile_programs() {
    log_info "编译程序..."
    
    # 确保build目录存在
    mkdir -p build
    cd build
    
    # 运行CMake和Make
    cmake ..
    if [ $? -ne 0 ]; then
        log_error "CMake配置失败"
        exit 1
    fi
    
    make -j$(nproc)
    if [ $? -ne 0 ]; then
        log_error "编译失败"
        exit 1
    fi
    
    cd ..
    log_info "编译完成"
}

# 启动程序
start_programs() {
    log_info "启动程序..."
    
    cd build
    
    # 启动订阅者（MQTT桥接）
    log_info "启动 FastDDS 订阅者..."
    ./fastdds_subscriber &
    SUBSCRIBER_PID=$!
    echo $SUBSCRIBER_PID > /tmp/fastdds_subscriber.pid
    log_info "FastDDS 订阅者已启动 (PID: $SUBSCRIBER_PID)"
    
    # 等待订阅者初始化
    log_info "等待订阅者初始化..."
    sleep 5
    
    # 启动发布者
    log_info "启动 FastDDS 发布者..."
    ./fastdds_publisher &
    PUBLISHER_PID=$!
    echo $PUBLISHER_PID > /tmp/fastdds_publisher.pid
    log_info "FastDDS 发布者已启动 (PID: $PUBLISHER_PID)"
}

# 显示状态
show_status() {
    log_info "检查程序状态..."
    
    if check_running "fastdds_subscriber"; then
        local pid=$(pgrep -f "fastdds_subscriber")
        log_info "✓ FastDDS 订阅者运行中 (PID: $pid)"
    else
        log_error "✗ FastDDS 订阅者未运行"
    fi
    
    if check_running "fastdds_publisher"; then
        local pid=$(pgrep -f "fastdds_publisher")
        log_info "✓ FastDDS 发布者运行中 (PID: $pid)"
    else
        log_error "✗ FastDDS 发布者未运行"
    fi
}

# 主函数
main() {
    echo "=========================================="
    echo "    FastDDS 发送端启动脚本"
    echo "=========================================="
    
    # 停止现有程序
    stop_programs
    
    # 编译程序
    compile_programs
    
    # 启动程序
    start_programs
    
    # 显示状态
    show_status
    
    echo ""
    echo "=========================================="
    log_info "发送端启动完成！"
    log_info "使用 './stop_sender.sh' 停止所有程序"
    echo "=========================================="
}

# 运行主函数
main "$@"