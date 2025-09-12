#!/usr/bin/env python3

import time
import threading
import signal
import sys
from dataclasses import dataclass
from typing import Dict

import fastdds
from PingMessage import PingMessage
from PingMessagePubSubTypes import PingMessagePubSubType

@dataclass
class PingStats:
    sequence_id: int
    send_time: float
    rtt: float = 0.0
    latency: float = 0.0

class PingClient:
    def __init__(self, domain_id: int = 0):
        self.domain_id = domain_id
        self.sequence_id = 0
        self.pending_pings: Dict[int, PingStats] = {}
        self.stats_lock = threading.Lock()
        
        # 创建DDS实体
        self.participant = None
        self.ping_publisher = None
        self.pong_subscriber = None
        self.running = True
        
        # 统计信息
        self.total_pings = 0
        self.successful_pongs = 0
        self.min_latency = float('inf')
        self.max_latency = 0.0
        self.avg_latency = 0.0
        
    def setup_dds(self):
        """设置DDS发布者和订阅者"""
        factory = fastdds.DomainParticipantFactory.get_instance()
        
        # 创建参与者
        participant_qos = fastdds.DomainParticipantQos()
        self.participant = factory.create_participant(self.domain_id, participant_qos)
        
        # 注册类型
        ping_type = PingMessagePubSubType()
        self.participant.register_type(ping_type)
        
        # 创建主题
        ping_topic_qos = fastdds.TopicQos()
        ping_topic = self.participant.create_topic("PingTopic", "PingMessage", ping_topic_qos)
        pong_topic = self.participant.create_topic("PongTopic", "PingMessage", ping_topic_qos)
        
        # 创建发布者和订阅者
        self.ping_publisher = self.participant.create_publisher(fastdds.PublisherQos())
        self.pong_subscriber = self.participant.create_subscriber(fastdds.SubscriberQos())
        
        # 创建数据写入器
        writer_qos = fastdds.DataWriterQos()
        writer_qos.reliability().kind = fastdds.ReliabilityQosPolicyKind.RELIABLE_RELIABILITY_QOS
        self.ping_writer = self.ping_publisher.create_datawriter(ping_topic, writer_qos)
        
        # 创建数据读取器
        reader_qos = fastdds.DataReaderQos()
        reader_qos.reliability().kind = fastdds.ReliabilityQosPolicyKind.RELIABLE_RELIABILITY_QOS
        
        class PongListener(fastdds.DataReaderListener):
            def __init__(self, client):
                super().__init__()
                self.client = client
                
            def on_data_available(self, reader):
                info = fastdds.SampleInfo()
                msg = PingMessage()
                if reader.take_next_sample(msg, info) == fastdds.ReturnCode_t.RETCODE_OK:
                    if msg.message_type() == "PONG":
                        self.client.handle_pong(msg)
        
        self.pong_reader = self.pong_subscriber.create_datareader(
            pong_topic, reader_qos, PongListener(self))
        
    def send_ping(self):
        """发送一个ping消息"""
        self.sequence_id += 1
        current_time = time.time_ns()
        
        # 创建ping消息
        ping_msg = PingMessage()
        ping_msg.sequence_id(self.sequence_id)
        ping_msg.client_timestamp(current_time)
        ping_msg.message_type("PING")
        
        # 记录发送信息
        with self.stats_lock:
            self.pending_pings[self.sequence_id] = PingStats(
                sequence_id=self.sequence_id,
                send_time=current_time / 1e9  # 转换为秒
            )
        
        # 发送消息
        self.ping_writer.write(ping_msg)
        self.total_pings += 1
        
        print(f"发送 PING #{self.sequence_id}")
        
    def handle_pong(self, pong_msg):
        """处理收到的pong消息"""
        receive_time = time.time_ns() / 1e9  # 转换为秒
        seq_id = pong_msg.sequence_id()
        
        with self.stats_lock:
            if seq_id in self.pending_pings:
                ping_stats = self.pending_pings.pop(seq_id)
                
                # 计算延时
                rtt = receive_time - ping_stats.send_time
                latency = rtt / 2  # 单向延时
                
                ping_stats.rtt = rtt
                ping_stats.latency = latency
                
                # 更新统计信息
                self.successful_pongs += 1
                self.min_latency = min(self.min_latency, latency)
                self.max_latency = max(self.max_latency, latency)
                self.avg_latency = ((self.avg_latency * (self.successful_pongs - 1)) + latency) / self.successful_pongs
                
                print(f"收到 PONG #{seq_id}: RTT={rtt*1000:.2f}ms, 延时={latency*1000:.2f}ms")
            else:
                print(f"收到未知序列号的PONG: #{seq_id}")
    
    def print_statistics(self):
        """打印统计信息"""
        print("\n=== 延时测量统计 ===")
        print(f"发送PING: {self.total_pings}")
        print(f"收到PONG: {self.successful_pongs}")
        print(f"丢包率: {((self.total_pings - self.successful_pongs) / self.total_pings * 100):.1f}%")
        if self.successful_pongs > 0:
            print(f"最小延时: {self.min_latency*1000:.2f}ms")
            print(f"最大延时: {self.max_latency*1000:.2f}ms")
            print(f"平均延时: {self.avg_latency*1000:.2f}ms")
    
    def run(self, interval: float = 1.0, count: int = 10):
        """运行ping测试"""
        self.setup_dds()
        
        def signal_handler(signum, frame):
            print("\n正在停止...")
            self.running = False
        
        signal.signal(signal.SIGINT, signal_handler)
        
        print(f"开始ping测试，间隔{interval}秒，总计{count}次")
        
        for i in range(count):
            if not self.running:
                break
                
            self.send_ping()
            time.sleep(interval)
        
        # 等待剩余的pong
        print("等待剩余响应...")
        time.sleep(2)
        
        self.print_statistics()

if __name__ == "__main__":
    client = PingClient()
    client.run(interval=1.0, count=10)