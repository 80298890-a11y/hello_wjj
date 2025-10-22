#!/bin/bash

# 全项目构建脚本
# 自动构建 fastdds_lib 库和 test 程序
# 使用方法: ./build_all.sh [clean]

set -e  # 出错立即退出

PROJECT_ROOT="/home/hello/work/project_root"
FASTDDS_LIB_DIR="$PROJECT_ROOT/fastdds_lib"
TEST_DIR="$PROJECT_ROOT/test"

# 颜色定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# 清理函数
clean_build() {
    local dir=$1
    local name=$2
    
    log_info "清理 $name 构建目录..."
    if [ -d "$dir/build" ]; then
        rm -rf "$dir/build"
        log_success "$name 构建目录已清理"
    else
        log_warning "$name 构建目录不存在，跳过清理"
    fi
}

# 构建函数
build_project() {
    local dir=$1
    local name=$2
    
    log_info "开始构建 $name..."
    
    # 进入项目目录
    cd "$dir"
    
    # 创建并进入build目录
    mkdir -p build
    cd build
    
    # 设置FastDDS环境变量
    export CMAKE_PREFIX_PATH="/usr/local/fastdds-2.6.10:$CMAKE_PREFIX_PATH"
    export LD_LIBRARY_PATH="/usr/local/fastdds-2.6.10/lib:$LD_LIBRARY_PATH"
    export PATH="/usr/local/fastdds-2.6.10/bin:$PATH"
    
    # CMake配置
    log_info "$name: 配置CMake..."
    if cmake .. > /dev/null 2>&1; then
        log_success "$name: CMake配置成功"
    else
        log_error "$name: CMake配置失败"
        cmake ..  # 显示详细错误
        exit 1
    fi
    
    # 编译
    log_info "$name: 开始编译..."
    if make -j$(nproc) > /dev/null 2>&1; then
        log_success "$name: 编译成功"
    else
        log_error "$name: 编译失败"
        make -j$(nproc)  # 显示详细错误
        exit 1
    fi
    
    # 返回项目根目录
    cd "$PROJECT_ROOT"
}

# 创建XML配置文件
create_xml_configs() {
    log_info "创建XML配置文件到test/build目录..."
    
    # 创建publisher.xml
    cat > "$TEST_DIR/build/publisher.xml" << 'EOF'
<?xml version="1.0" encoding="utf-8" ?>
<dds xmlns="http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles">
    <profiles>
        <transport_descriptors>
            <transport_descriptor>
                <transport_id>tcp_transport_server</transport_id>
                <type>TCPv4</type>
                <listening_ports>
                    <port>5101</port>
                </listening_ports>
                <wan_addr>192.168.2.90</wan_addr>
            </transport_descriptor>
        </transport_descriptors>

        <participant profile_name="pub_profile" is_default_profile="true">
            <rtps>
                <userTransports>
                    <transport_id>tcp_transport_server</transport_id>
                </userTransports>
                <useBuiltinTransports>false</useBuiltinTransports>
            </rtps>
        </participant>
    </profiles>
</dds>
EOF
    
    # 创建subscriber.xml
    cat > "$TEST_DIR/build/subscriber.xml" << 'EOF'
<dds xmlns="http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles">
    <profiles>
        <transport_descriptors>
            <transport_descriptor>
                <transport_id>tcp_transport_client</transport_id>
                <type>TCPv4</type>
            </transport_descriptor>
        </transport_descriptors>

        <participant profile_name="sub_profile" is_default_profile="true">
            <rtps>
                <userTransports>
                    <transport_id>tcp_transport_client</transport_id>
                </userTransports>
                <useBuiltinTransports>false</useBuiltinTransports>
                <builtin>
                    <initialPeersList>
                        <locator>
                            <tcpv4>
                                <address>192.168.2.102</address>
                                <physical_port>5100</physical_port>
                            </tcpv4>
                        </locator>
                    </initialPeersList>
                </builtin>
            </rtps>
        </participant>
    </profiles>
</dds>
EOF
    
    log_success "XML配置文件创建完成"
}

# 显示构建结果
show_build_result() {
    echo ""
    log_success "=== 构建完成 ==="
    echo ""
    
    log_info "FastDDS库文件:"
    if [ -f "$FASTDDS_LIB_DIR/build/libfastdds_lib.a" ]; then
        echo -e "  ${GREEN}✓${NC} $FASTDDS_LIB_DIR/build/libfastdds_lib.a"
    else
        echo -e "  ${RED}✗${NC} 库文件未找到"
    fi
    
    echo ""
    log_info "Test可执行文件:"
    local executables=(
        "bridge_forwarder"
        "fastdds_to_mqtt_bridge"
        "mqtt_receiver"
        "mqtt_sender"
        "mqtt_to_fastdds_bridge"
        "publisher"
        "subscriber"
        "tsp_command_processor"
        "vehicle_instruction_takeover"
        "vehicle_instruction_braking"
        "vehicle_instruction_lateral"
        "vehicle_instruction_distance_time"
    )
    
    for exe in "${executables[@]}"; do
        if [ -f "$TEST_DIR/build/$exe" ]; then
            echo -e "  ${GREEN}✓${NC} $exe"
        else
            echo -e "  ${RED}✗${NC} $exe (未找到)"
        fi
    done
    
    echo ""
    log_info "XML配置文件:"
    if [ -f "$TEST_DIR/build/publisher.xml" ] && [ -f "$TEST_DIR/build/subscriber.xml" ]; then
        echo -e "  ${GREEN}✓${NC} publisher.xml, subscriber.xml"
    else
        echo -e "  ${YELLOW}!${NC} XML配置文件可能需要手动配置"
    fi
}

# 主函数
main() {
    echo "========================================"
    echo "      FastDDS项目全量构建脚本"
    echo "========================================"
    echo ""
    
    # 检查是否为clean模式
    if [ "$1" = "clean" ]; then
        log_warning "执行清理构建模式..."
        clean_build "$FASTDDS_LIB_DIR" "FastDDS库"
        clean_build "$TEST_DIR" "Test程序"
        echo ""
    fi
    
    # 检查项目目录
    if [ ! -d "$FASTDDS_LIB_DIR" ] || [ ! -d "$TEST_DIR" ]; then
        log_error "项目目录不存在，请检查路径配置"
        exit 1
    fi
    
    # 构建FastDDS库
    build_project "$FASTDDS_LIB_DIR" "FastDDS库"
    
    # 构建Test程序
    build_project "$TEST_DIR" "Test程序"
    
    # 创建配置文件
    create_xml_configs
    
    # 显示构建结果
    show_build_result
    
    echo ""
    log_success "全部构建完成！可执行文件位于: $TEST_DIR/build/"
    log_info "运行示例: cd $TEST_DIR/build && ./publisher"
}

# 执行主函数
main "$@"
