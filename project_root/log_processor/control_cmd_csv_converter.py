#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
control_cmd日志转CSV格式转换器
将MQTT发送日志中的control_cmd数据转换为CSV格式便于分析
"""

import json
import csv
import re
import sys
import os
from datetime import datetime
import argparse

def parse_log_line(line):
    """解析单行日志，提取时间戳和JSON数据"""
    # 匹配时间戳格式：[HH:MM:SS]
    time_match = re.match(r'\[(\d{2}:\d{2}:\d{2})\]', line.strip())
    if not time_match:
        return None
    
    log_time = time_match.group(1)
    
    # 查找JSON部分
    json_start = line.find('{')
    if json_start == -1:
        return None
    
    json_str = line[json_start:].strip()
    
    try:
        json_data = json.loads(json_str)
        json_data['log_time'] = log_time
        return json_data
    except json.JSONDecodeError:
        return None

def convert_log_to_csv(input_file, output_file):
    """将日志文件转换为CSV格式"""
    
    print(f"📖 开始读取日志文件: {input_file}")
    
    if not os.path.exists(input_file):
        print(f"❌ 错误: 日志文件不存在: {input_file}")
        return False
    
    # 定义CSV列名（基于RemoteControl.idl结构）
    csv_columns = [
        'log_time',                      # 日志时间戳
        'steering_angle_enable',         # 转向角使能
        'steering_angle',               # 转向角度
        'target_acceleration_enable',    # 加速度使能
        'target_acceleration',          # 目标加速度
        'indicator_left_enable',        # 左转向灯使能
        'indicator_left',               # 左转向灯
        'indicator_right_enable',       # 右转向灯使能
        'indicator_right',              # 右转向灯
        'gear_position_enable',         # 档位使能
        'gear_position',                # 档位
        'ebrake_status_enable',         # 手刹使能
        'ebrake_status',               # 手刹状态
        'lane_change',                 # 变道
        'timestamp',                   # IDL时间戳
        'mqtt_send_timestamp'          # MQTT发送时间戳
    ]
    
    data_rows = []
    processed_lines = 0
    valid_lines = 0
    
    try:
        with open(input_file, 'r', encoding='utf-8') as f:
            for line_num, line in enumerate(f, 1):
                processed_lines += 1
                
                # 解析日志行
                data = parse_log_line(line)
                if data:
                    valid_lines += 1
                    # 简单地将JSON字段映射到CSV列（不做逻辑处理）
                    row = {}
                    for col in csv_columns:
                        # 直接获取字段值，没有的设为空字符串
                        row[col] = data.get(col, "")
                    data_rows.append(row)
                
                # 每1000行显示进度
                if processed_lines % 1000 == 0:
                    print(f"⏳ 已处理 {processed_lines} 行，有效数据 {valid_lines} 行")
        
        print(f"✅ 日志读取完成: 总计 {processed_lines} 行，有效数据 {valid_lines} 行")
        
        # 写入CSV文件
        print(f"💾 开始写入CSV文件: {output_file}")
        
        with open(output_file, 'w', newline='', encoding='utf-8-sig') as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=csv_columns)
            writer.writeheader()
            writer.writerows(data_rows)
        
        print(f"🎉 转换完成!")
        print(f"📊 输入文件: {input_file}")
        print(f"📊 输出文件: {output_file}")
        print(f"📊 数据行数: {len(data_rows)}")
        print(f"📊 CSV列数: {len(csv_columns)}")
        
        return True
        
    except Exception as e:
        print(f"❌ 转换失败: {str(e)}")
        return False

def main():
    parser = argparse.ArgumentParser(description='将control_cmd日志转换为CSV格式')
    parser.add_argument('input_file', help='输入的日志文件路径')
    parser.add_argument('-o', '--output', help='输出的CSV文件路径（可选）')
    
    args = parser.parse_args()
    
    input_file = args.input_file
    
    # 生成输出文件名
    if args.output:
        output_file = args.output
    else:
        # 自动生成输出文件名
        base_name = os.path.splitext(os.path.basename(input_file))[0]
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        output_file = os.path.join(
            os.path.dirname(os.path.abspath(__file__)), 
            f"{base_name}_{timestamp}.csv"
        )
    
    print("=== control_cmd日志转CSV转换器 ===")
    print(f"输入文件: {input_file}")
    print(f"输出文件: {output_file}")
    print("")
    
    # 执行转换
    success = convert_log_to_csv(input_file, output_file)
    
    if success:
        print(f"\n✅ 转换成功! CSV文件已保存到: {output_file}")
        sys.exit(0)
    else:
        print(f"\n❌ 转换失败!")
        sys.exit(1)

if __name__ == '__main__':
    main()


