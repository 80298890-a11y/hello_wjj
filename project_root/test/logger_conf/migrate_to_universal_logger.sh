#!/bin/bash

# 通用日志系统迁移脚本
# Universal Logger Migration Script

echo "=========================================="
echo "     通用日志系统迁移工具"
echo "=========================================="
echo ""

if [ $# -ne 1 ]; then
    echo "用法: $0 <cpp文件路径>"
    echo "示例: $0 ../mqtt_sender.cpp"
    exit 1
fi

CPP_FILE="$1"

if [ ! -f "$CPP_FILE" ]; then
    echo "❌ 错误: 文件 $CPP_FILE 不存在"
    exit 1
fi

echo "🔍 分析文件: $CPP_FILE"

# 创建备份
BACKUP_FILE="${CPP_FILE}.backup.$(date +%Y%m%d_%H%M%S)"
cp "$CPP_FILE" "$BACKUP_FILE"
echo "📋 已创建备份: $BACKUP_FILE"

# 检查是否已经使用通用日志系统
if grep -q "universal_logger.h" "$CPP_FILE"; then
    echo "✅ 该文件已经使用通用日志系统"
    exit 0
fi

echo ""
echo "🔧 开始迁移..."

# 1. 替换include语句
echo "  📝 替换include语句..."
sed -i 's|#include "async_logger.hpp"|#include "logger_conf/universal_logger.h"|g' "$CPP_FILE"

# 2. 删除AsyncLogger声明
echo "  🗑️  删除旧的日志器声明..."
sed -i '/std::unique_ptr<AsyncLogger> g_logger/d' "$CPP_FILE"

# 3. 替换日志器初始化
echo "  🔄 替换日志器初始化..."
# 检测程序名称
PROGRAM_NAME=$(basename "$CPP_FILE" .cpp)
sed -i "s|g_logger = std::make_unique<AsyncLogger>(\".*\")|initUniversalLogger(\"$PROGRAM_NAME\")|g" "$CPP_FILE"

# 4. 替换日志调用
echo "  📨 替换日志调用..."
sed -i 's|g_logger->log("INFO", \(.*\))|LOG_INFO(\1)|g' "$CPP_FILE"
sed -i 's|g_logger->log("DEBUG", \(.*\))|LOG_DEBUG(\1)|g' "$CPP_FILE"
sed -i 's|g_logger->log("WARN", \(.*\))|LOG_WARN(\1)|g' "$CPP_FILE"
sed -i 's|g_logger->log("ERROR", \(.*\))|LOG_ERROR(\1)|g' "$CPP_FILE"

# 5. 在main函数结束前添加清理代码
echo "  🧹 添加清理代码..."
# 查找main函数的return 0语句，并在前面添加清理代码
if grep -q "return 0" "$CPP_FILE"; then
    sed -i '/return 0/i\    cleanupUniversalLogger();' "$CPP_FILE"
fi

echo ""
echo "✅ 迁移完成!"
echo ""
echo "📋 迁移总结:"
echo "  ✓ 替换了include语句"
echo "  ✓ 删除了旧的日志器声明"
echo "  ✓ 更新了日志器初始化"
echo "  ✓ 替换了所有日志调用"
echo "  ✓ 添加了清理代码"
echo ""
echo "🔨 编译说明:"
echo "  需要编译并链接通用日志系统:"
echo "  g++ -c logger_conf/universal_logger.cpp -o universal_logger.o -std=c++17"
echo "  g++ -o $PROGRAM_NAME $CPP_FILE universal_logger.o -I. -std=c++17 -lpthread [其他库]"
echo ""
echo "⚠️  注意事项:"
echo "  1. 请检查生成的代码是否正确"
echo "  2. 如有问题，可以从备份文件恢复: $BACKUP_FILE"
echo "  3. 请确保logger_conf/log_config.yaml中有该程序的配置"
echo ""
echo "🎯 配置文件位置: logger_conf/log_config.yaml"
echo "📖 详细文档: logger_conf/README.md"
