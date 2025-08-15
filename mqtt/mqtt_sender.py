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

class MQTTSender:
    def __init__(self, broker="hellorobotaxi.cn", port=11883, topic="cloud/vehicle/control"):
        self.broker = broker
        self.port = port
        self.topic = topic
        self.client = mqtt.Client()
        self.connected = False
        self.running = True
        self.sequence_id = 0
        self.interval = 0.01  # 10ms
        
        # 创建CSV数据队列
        self.csv_queue = Queue()
        
        # 确保logs目录存在
        if not os.path.exists("logs"):
            os.makedirs("logs")
            
        # 创建带时间戳的日志目录
        timestamp = time.strftime('%Y%m%d_%H%M%S')
        self.log_dir = f"logs/sender_{timestamp}"
        while os.path.exists(self.log_dir):  # 避免目录冲突
            time.sleep(0.1)
            timestamp = time.strftime('%Y%m%d_%H%M%S')
            self.log_dir = f"logs/sender_{timestamp}"
        os.makedirs(self.log_dir)
        
        # 创建并初始化CSV文件
        self.csv_file = os.path.join(self.log_dir, "sender_data.csv")
        with open(self.csv_file, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['sequence_id', 'a1_timestamp'])
        
        # 启动CSV写入线程
        self.csv_thread = Thread(target=self._csv_writer, daemon=True)
        self.csv_thread.start()

    def on_connect(self, client, userdata, flags, rc):
        self.connected = rc == 0

    def get_current_timestamp(self):
        # 使用系统时间生成毫秒级Unix时间戳
        timestamp_ms = int(time.time() * 1000)
        return timestamp_ms

    def send_message(self):
        if not self.connected:
            return
        
        try:
            # 获取时间戳并发送消息
            self.sequence_id += 1
            a1_timestamp = self.get_current_timestamp()
            
            # 使用与publisher_template.cpp相同的消息格式，但将timestamp替换为a1_timestamp
            message = {
                "sequence_id": self.sequence_id,
                "steering_angle_enable": True,
                "steering_angle": 0.0,
                "target_acceleration_enable": True,
                "target_acceleration": 0.0,
                "indicator_left_enable": False,
                "indicator_left": False,
                "indicator_right_enable": False,
                "indicator_right": False,
                "gear_position_enable": False,
                "gear_position": 0,
                "ebrake_status_enable": False,
                "ebrake_status": 0,
                "remote_drive_ready": False,
                "remote_drive_takeover_request": False,
                "a1_timestamp": a1_timestamp  # 直接使用a1_timestamp替换timestamp
            }
            
            # 发送消息
            payload = json.dumps(message, ensure_ascii=False)
            self.client.publish(self.topic, payload, qos=1)
            
            # 每次发送后立即刷新
            self.client.loop_write()  # 立即写入网络
            
            # 记录sequence_id和a1_timestamp
            self.csv_queue.put([self.sequence_id, a1_timestamp])
            
        except Exception as e:
            print(f"Error sending message: {e}")

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
        try:
            self.client.on_connect = self.on_connect
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_start()

            while self.running:
                start_time = time.perf_counter()
                self.send_message()
                send_duration = time.perf_counter() - start_time
                sleep_time = max(0, self.interval - send_duration)
                if sleep_time > 0:
                    time.sleep(sleep_time)
        except Exception as e:
            print(f"Error in run loop: {e}")
            self.cleanup()

    def cleanup(self):
        self.running = False
        self.csv_queue.put(None)
        self.csv_thread.join(timeout=1)
        if self.connected:
            self.client.loop_stop()
            self.client.disconnect()

def signal_handler(signum, frame):
    if mqtt_sender:
        mqtt_sender.running = False

if __name__ == "__main__":
    mqtt_sender = None
    try:
        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)
        
        mqtt_sender = MQTTSender()
        mqtt_sender.run()
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
    finally:
        if mqtt_sender:
            mqtt_sender.cleanup()
        sys.exit(0)