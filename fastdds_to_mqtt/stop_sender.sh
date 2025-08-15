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

# 停止程序
stop_program() {
    local name=$1
    local pattern=$2
    
    if pgrep -f "$pattern" > /dev/null; then
        log_info "停止 $name..."
        pkill -f "$pattern"
        sleep 1
        
        if pgrep -f "$pattern" > /dev/null; then
            log_warn "强制停止 $name..."
            pkill -9 -f "$pattern"
        fi
        
        log_info "$name 已停止"
    else
        log_info "$name 未运行"
    fi
}

# 清理PID文件
cleanup_pid_files() {
    rm -f /tmp/fastdds_subscriber.pid
    rm -f /tmp/fastdds_publisher.pid
}

# 显示最终状态
show_final_status() {
    log_info "检查最终状态..."
    
    if pgrep -f "fastdds_subscriber" > /dev/null; then
        log_error "✗ FastDDS 订阅者仍在运行"
    else
        log_info "✓ FastDDS 订阅者已停止"
    fi
    
    if pgrep -f "fastdds_publisher" > /dev/null; then
        log_error "✗ FastDDS 发布者仍在运行"
    else
        log_info "✓ FastDDS 发布者已停止"
    fi
}

# 主函数
main() {
    echo "=========================================="
    echo "    FastDDS 发送端停止脚本"
    echo "=========================================="
    
    # 停止程序
    stop_program "FastDDS 订阅者" "fastdds_subscriber"
    stop_program "FastDDS 发布者" "fastdds_publisher"
    
    # 清理PID文件
    cleanup_pid_files
    
    # 显示最终状态
    show_final_status
    
    echo ""
    echo "=========================================="
    log_info "所有程序已停止"
    echo "=========================================="
}

# 运行主函数
main "$@"