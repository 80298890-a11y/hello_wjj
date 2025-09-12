import pandas as pd
import os
import glob
from datetime import datetime

class MQTTAnalyzer:
    def __init__(self, logs_dir="logs"):
        self.logs_dir = logs_dir

    def find_latest_test_dirs(self):
        """查找最新的测试目录"""
        sender_dirs = glob.glob(os.path.join(self.logs_dir, "sender_*"))
        receiver_dirs = glob.glob(os.path.join(self.logs_dir, "receiver_*"))
        
        if not sender_dirs or not receiver_dirs:
            print("未找到发送端或接收端日志目录")
            return None, None
        
        # 按时间戳排序，取最新的
        sender_dirs.sort()
        receiver_dirs.sort()
        
        latest_sender = sender_dirs[-1]
        latest_receiver = receiver_dirs[-1]
        
        return latest_sender, latest_receiver

    def load_data(self):
        """加载CSV数据"""
        sender_dir, receiver_dir = self.find_latest_test_dirs()
        
        if not sender_dir or not receiver_dir:
            return None, None
        
        try:
            # 加载发送端数据
            sender_file = os.path.join(sender_dir, "sender_data.csv")
            sender_data = pd.read_csv(sender_file)
            
            # 加载接收端数据
            receiver_file = os.path.join(receiver_dir, "receiver_data.csv")
            receiver_data = pd.read_csv(receiver_file)
            
            print(f"发送端数据: {len(sender_data)} 条记录")
            print(f"接收端数据: {len(receiver_data)} 条记录")
            
            return sender_data, receiver_data
            
        except Exception as e:
            print(f"加载数据时出错: {e}")
            return None, None

    def analyze_interval_stability(self, data, expected_interval=None):
        """分析时间间隔的稳定性"""
        valid_data = pd.to_numeric(data, errors='coerce').dropna()
        
        if len(valid_data) == 0:
            return None
            
        mean_interval = valid_data.mean()
        std_interval = valid_data.std()
        max_interval = valid_data.max()
        min_interval = valid_data.min()
        
        # 统计两个等级的异常
        mild_outliers = valid_data[(valid_data > 15) & (valid_data <= 20)]
        severe_outliers = valid_data[valid_data > 20]
        
        result = {
            "mean": mean_interval,
            "std": std_interval,
            "max": max_interval,
            "min": min_interval,
            "stability": "稳定" if std_interval < mean_interval * 0.1 else "不稳定",
            "mild_outliers_count": len(mild_outliers),
            "severe_outliers_count": len(severe_outliers),
            "total_outliers_count": len(mild_outliers) + len(severe_outliers),
            "total_samples": len(valid_data),
            "worst_cases": sorted(severe_outliers.nlargest(5).tolist(), reverse=True)
        }
        
        if expected_interval:
            result["deviation"] = abs(mean_interval - expected_interval)
            result["deviation_percent"] = (result["deviation"] / expected_interval) * 100
        
        return result

    def analyze_data(self):
        """分析数据并生成报告"""
        sender_data, receiver_data = self.load_data()
        
        if sender_data is None or receiver_data is None:
            print("无法加载数据")
            return
        
        print("\n" + "="*60)
        print("MQTT延迟测试分析报告")
        print("="*60)
        
        # 1. 发送端性能分析
        print("\n## 1. 发送端性能分析 (a1间隔)")
        if 'a1_timestamp' in sender_data.columns:
            # 计算a1间隔
            a1_intervals = sender_data['a1_timestamp'].diff().dropna()
            a1_interval = self.analyze_interval_stability(a1_intervals, expected_interval=10)
            
            if a1_interval:
                print(f"- 目标发送间隔: 10ms")
                print(f"- 平均间隔: {a1_interval['mean']:.2f}ms")
                print(f"- 间隔波动范围: {a1_interval['min']:.2f}ms - {a1_interval['max']:.2f}ms")
                print(f"- 标准差: {a1_interval['std']:.2f}ms")
                print(f"- 稳定性: {a1_interval['stability']}")
                print(f"- 异常数据统计:")
                print(f"  * 轻度异常(15-20ms): {a1_interval['mild_outliers_count']} 条")
                print(f"  * 严重异常(>20ms): {a1_interval['severe_outliers_count']} 条")
                print(f"  * 总异常: {a1_interval['total_outliers_count']} 条")
                if a1_interval['worst_cases']:
                    print(f"  * 最严重异常: {a1_interval['worst_cases']}")
        
        # 2. 接收端性能分析
        print("\n## 2. 接收端性能分析 (b1间隔)")
        if 'b1_timestamp' in receiver_data.columns:
            # 计算b1间隔
            b1_intervals = receiver_data['b1_timestamp'].diff().dropna()
            b1_interval = self.analyze_interval_stability(b1_intervals, expected_interval=10)
            
            if b1_interval:
                print(f"- 目标接收间隔: 10ms")
                print(f"- 平均间隔: {b1_interval['mean']:.2f}ms")
                print(f"- 间隔波动范围: {b1_interval['min']:.2f}ms - {b1_interval['max']:.2f}ms")
                print(f"- 标准差: {b1_interval['std']:.2f}ms")
                print(f"- 稳定性: {b1_interval['stability']}")
                print(f"- 异常数据统计:")
                print(f"  * 轻度异常(15-20ms): {b1_interval['mild_outliers_count']} 条")
                print(f"  * 严重异常(>20ms): {b1_interval['severe_outliers_count']} 条")
                print(f"  * 总异常: {b1_interval['total_outliers_count']} 条")
                if b1_interval['worst_cases']:
                    print(f"  * 最严重异常: {b1_interval['worst_cases']}")
        
        # 3. 传输延迟分析
        print("\n## 3. 传输延迟分析 (b1-a1)")
        if 'a1_timestamp' in receiver_data.columns and 'b1_timestamp' in receiver_data.columns:
            # 计算传输延迟
            latency = receiver_data['b1_timestamp'] - receiver_data['a1_timestamp']
            latency_analysis = self.analyze_interval_stability(latency)
            
            if latency_analysis:
                print(f"- 平均延迟: {latency_analysis['mean']:.2f}ms")
                print(f"- 延迟范围: {latency_analysis['min']:.2f}ms - {latency_analysis['max']:.2f}ms")
                print(f"- 标准差: {latency_analysis['std']:.2f}ms")
                print(f"- 延迟稳定性: {latency_analysis['stability']}")
                print(f"- 异常延迟统计:")
                print(f"  * 轻度异常(15-20ms): {latency_analysis['mild_outliers_count']} 条")
                print(f"  * 严重异常(>20ms): {latency_analysis['severe_outliers_count']} 条")
                print(f"  * 总异常: {latency_analysis['total_outliers_count']} 条")
                if latency_analysis['worst_cases']:
                    print(f"  * 最严重延迟: {latency_analysis['worst_cases']}")
        
        # 4. 性能评估和建议
        print("\n## 4. 性能评估和建议")
        print("- 发送端性能:")
        if a1_interval:
            if a1_interval['mean'] < 11:
                print("  * 发送间隔控制良好")
            else:
                print("  * 发送间隔控制需要优化")
        
        print("- 接收端性能:")
        if b1_interval:
            if b1_interval['mean'] < 11:
                print("  * 接收间隔稳定")
            else:
                print("  * 接收间隔不稳定，可能存在丢包")
        
        print("- 网络延迟:")
        if latency_analysis:
            if latency_analysis['mean'] < 50:
                print("  * 网络延迟较低，性能良好")
            elif latency_analysis['mean'] < 100:
                print("  * 网络延迟中等，可接受")
            else:
                print("  * 网络延迟较高，需要优化网络环境")
        
        print("\n## 5. 改进建议")
        print("1. 如果发送间隔不稳定，建议:")
        print("   - 检查系统负载")
        print("   - 优化发送逻辑")
        print("   - 考虑使用更高精度的定时器")
        
        print("2. 如果接收间隔不稳定，建议:")
        print("   - 检查网络连接质量")
        print("   - 优化接收端处理逻辑")
        print("   - 考虑增加缓冲区大小")
        
        print("3. 如果传输延迟过高，建议:")
        print("   - 检查网络带宽")
        print("   - 优化MQTT服务器配置")
        print("   - 考虑使用更近的服务器")

if __name__ == "__main__":
    analyzer = MQTTAnalyzer()
    analyzer.analyze_data()