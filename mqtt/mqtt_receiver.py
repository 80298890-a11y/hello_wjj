import paho.mqtt.client as mqtt
import json
import time
import signal
import sys
from datetime import datetime
import os
from queue import Queue
from threading import Thread
import csv

class MQTTReceiver:
    def __init__(self, broker="hellorobotaxi.cn", port=11883, topic="cloud/vehicle/control"):
        self.broker = broker
        self.port = port
        self.topic = topic
        self.client = mqtt.Client()
        self.connected = False
        self.running = True
        
        # 移除b1_sn，直接使用JSON中的sequence_id
        
        # 创建CSV数据队列
        self.csv_queue = Queue()
        
        # 确保logs目录存在
        if not os.path.exists("logs"):
            os.makedirs("logs")
            
        # 创建带时间戳的日志目录
        timestamp = time.strftime('%Y%m%d_%H%M%S')
        self.log_dir = f"logs/receiver_{timestamp}"
        while os.path.exists(self.log_dir):  # 避免目录冲突
            time.sleep(0.1)
            timestamp = time.strftime('%Y%m%d_%H%M%S')
            self.log_dir = f"logs/receiver_{timestamp}"
        os.makedirs(self.log_dir)
        
        # 创建并初始化CSV文件
        self.csv_file = os.path.join(self.log_dir, "receiver_data.csv")
        with open(self.csv_file, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['sequence_id', 'b1_timestamp'])
        
        # 启动CSV写入线程
        self.csv_thread = Thread(target=self._csv_writer, daemon=True)
        self.csv_thread.start()

    def fast_parse_json(self, payload):
        """快速解析JSON，提取sequence_id字段"""
        try:
            # 快速提取sequence_id
            seq_start = payload.find('"sequence_id":') + 14
            if seq_start == 13:  # 没找到
                return None
            
            seq_end = payload.find(',', seq_start)
            if seq_end == -1:
                seq_end = payload.find('}', seq_start)
            sequence_id = int(payload[seq_start:seq_end])
            
            return sequence_id
            
        except (ValueError, IndexError):
            # 如果快速解析失败，回退到JSON解析
            try:
                data = json.loads(payload)
                sequence_id = data.get('sequence_id', 0)
                return sequence_id
            except:
                return None

    def on_connect(self, client, userdata, flags, rc):
        self.connected = rc == 0
        if self.connected:
            self.client.subscribe(self.topic, qos=0)

    def get_current_timestamp(self):
        # 使用系统时间生成毫秒级Unix时间戳
        timestamp_ms = int(time.time() * 1000)
        return timestamp_ms

    def on_message(self, client, userdata, msg):
            try:
                # 立即记录接收时间戳
                b1_timestamp = self.get_current_timestamp()
                
                # 快速解析消息获取sequence_id
                payload = msg.payload.decode('utf-8')
                sequence_id = self.fast_parse_json(payload)
                
                # 确保解析成功
                if sequence_id is not None:
                    self.csv_queue.put([sequence_id, b1_timestamp])
                else:
                    print(f"Failed to parse message: {payload[:100]}...")
                
            except Exception as e:
                print(f"Error processing message: {e}")

    def _csv_writer(self):
        buffer = []
        max_buffer = 100  # 缓冲100条数据再写入
        
        try:
            f = open(self.csv_file, 'a', newline='')
            writer = csv.writer(f)
            
            while True:
                try:
                    data = self.csv_queue.get()
                    if data is None:
                        if buffer:
                            writer.writerows(buffer)
                            f.flush()
                        break
                    
                    buffer.append(data)
                    if len(buffer) >= max_buffer:
                        writer.writerows(buffer)
                        f.flush()
                        buffer = []
                        
                except Exception as e:
                    print(f"Error writing to CSV: {e}")
                    continue
                    
        finally:
            f.close()

    def run(self):
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        
        try:
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_forever()
        except Exception as e:
            print(f"Connection error: {e}")
            self.cleanup()

    def cleanup(self):
        self.running = False
        self.csv_queue.put(None)
        self.csv_thread.join(timeout=1)
        if self.connected:
            self.client.loop_stop()
            self.client.disconnect()

def signal_handler(signum, frame):
    if mqtt_receiver:
        mqtt_receiver.running = False
        mqtt_receiver.client.disconnect()

if __name__ == "__main__":
    mqtt_receiver = None
    try:
        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)
        
        mqtt_receiver = MQTTReceiver()
        mqtt_receiver.run()
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
    finally:
        if mqtt_receiver:
            mqtt_receiver.cleanup()
        sys.exit(0)