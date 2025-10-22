#!/usr/bin/env python3
"""本地MQTT到远程MQTT转发器"""

import json
import yaml
import logging
from logging.handlers import RotatingFileHandler
import paho.mqtt.client as mqtt
import signal
import sys
from pathlib import Path
import copy
import time

g_running = True
g_vid = ""
g_local_client = None
g_remote_client = None

g_last_messages = {"/handshake/response": None, "/vehicle/drive_state": None, "/vehicle/fault_report": None}
FORWARD_TOPICS = ["/handshake/response", "/vehicle/vehicle_status", "/vehicle/drive_state", "/vehicle/fault_report"]
LOCAL_HOST, LOCAL_PORT = "127.0.0.1", 1883
REMOTE_HOST, REMOTE_PORT = "hellorobotaxi.cn", 11883

topic_loggers = {}
main_logger = None

def read_vid_from_config():
    try:
        config_path = Path("car_config/vehicle_config.yaml")
        if config_path.exists():
            with open(config_path, 'r', encoding='utf-8') as f:
                config = yaml.safe_load(f)
                if config and 'vehicle' in config and 'vid' in config['vehicle']:
                    return str(config['vehicle']['vid'])
    except Exception as e:
        print(f"[ERROR] 读取配置文件失败: {e}")
    return "10001001"

def read_log_config():
    try:
        config_path = Path("logger_conf/log_config.yaml")
        if config_path.exists():
            with open(config_path, 'r', encoding='utf-8') as f:
                return yaml.safe_load(f)
    except:
        pass
    return {
        'global': {'base_dir': 'build/logs', 'max_storage_mb': 50, 'default_level': 'INFO'},
        'programs': {'local_to_remote_mqtt_forwarder': {'subdir': 'local_to_remote_mqtt_forwarder', 'level': 'INFO', 'max_files': 15}}
    }

def init_logger(program_name="local_to_remote_mqtt_forwarder"):
    global main_logger
    log_config = read_log_config()
    base_dir = log_config.get('global', {}).get('base_dir', 'build/logs')
    program_config = log_config.get('programs', {}).get(program_name, {})
    subdir = program_config.get('subdir', 'local_to_remote_mqtt_forwarder')
    level = program_config.get('level', 'INFO')
    
    log_dir = Path(base_dir) / subdir
    log_dir.mkdir(parents=True, exist_ok=True)
    
    main_logger = logging.getLogger('main')
    main_logger.setLevel(getattr(logging, level))
    console_handler = logging.StreamHandler()
    console_handler.setFormatter(logging.Formatter('[%(levelname)s] %(message)s'))
    main_logger.addHandler(console_handler)
    print(f"[SUCCESS] 日志系统初始化完成，日志目录: {log_dir.absolute()}")

def get_topic_logger(topic_path):
    if topic_path in topic_loggers:
        return topic_loggers[topic_path]
    
    log_config = read_log_config()
    base_dir = log_config.get('global', {}).get('base_dir', 'build/logs')
    program_config = log_config.get('programs', {}).get('local_to_remote_mqtt_forwarder', {})
    subdir = program_config.get('subdir', 'local_to_remote_mqtt_forwarder')
    max_files = program_config.get('max_files', 15)
    
    topic_dir = Path(base_dir) / subdir / topic_path.strip('/').replace('/', '_')
    topic_dir.mkdir(parents=True, exist_ok=True)
    
    logger = logging.getLogger(f'topic_{topic_path}')
    logger.setLevel(logging.INFO)
    logger.propagate = False
    
    log_file = topic_dir / f"{topic_path.strip('/').replace('/', '_')}.log"
    file_handler = RotatingFileHandler(log_file, maxBytes=5*1024*1024, backupCount=max_files)
    file_handler.setFormatter(logging.Formatter('[%(asctime)s] %(message)s', datefmt='%Y-%m-%d %H:%M:%S'))
    logger.addHandler(file_handler)
    
    topic_loggers[topic_path] = logger
    return logger

def remove_timestamps(data):
    if not isinstance(data, dict):
        return data
    cleaned = copy.deepcopy(data)
    for field in ['timestamp', 'fastdds_timestamp', 'received_timestamp', 'mqtt_received_timestamp', 'mqtt_processed_timestamp']:
        cleaned.pop(field, None)
    return cleaned

def should_forward(topic, content_without_timestamps):
    if topic in g_last_messages:
        if g_last_messages[topic] == content_without_timestamps:
            return False
        g_last_messages[topic] = content_without_timestamps
    return True

def on_local_message(client, userdata, msg):
    try:
        topic, payload = msg.topic, msg.payload.decode('utf-8')
        try:
            data = json.loads(payload)
        except json.JSONDecodeError as e:
            main_logger.error(f"JSON解析失败: {e}, payload: {payload}")
            return
        
        if not should_forward(topic, remove_timestamps(data)):
            return
        
        data['vid'] = g_vid
        enhanced_payload = json.dumps(data, ensure_ascii=False)
        
        # 添加vid到topic路径
        remote_topic = f"{topic}/{g_vid}"
        success = publish_to_remote(remote_topic, enhanced_payload)
        
        topic_name = topic.strip('/').replace('/', '_')
        get_topic_logger(topic_name).info(f"[{remote_topic}] {enhanced_payload}")
    except Exception as e:
        main_logger.error(f"处理消息失败: {e}")

def publish_to_remote(topic, message):
    if not g_remote_client or not g_remote_client.is_connected():
        print("[ERROR] 远程MQTT未连接")
        return False
    try:
        return g_remote_client.publish(topic, message, qos=0).rc == mqtt.MQTT_ERR_SUCCESS
    except Exception as e:
        print(f"[ERROR] 远程MQTT发布失败: {e}")
        return False

def on_local_connect(client, userdata, flags, rc):
    if rc == 0:
        print("[SUCCESS] 连接到本地MQTT成功")
        for topic in FORWARD_TOPICS:
            client.subscribe(topic, qos=0)
            print(f"[SUCCESS] 订阅topic: {topic}")
    else:
        print(f"[ERROR] 连接到本地MQTT失败: {rc}")

def on_local_disconnect(client, userdata, rc):
    print("[INFO] 本地MQTT正常断开连接" if rc == 0 else f"[WARN] 本地MQTT意外断开连接 (rc={rc})，将自动重连...")

def on_remote_connect(client, userdata, flags, rc):
    print("[SUCCESS] 连接到远程MQTT成功" if rc == 0 else f"[ERROR] 连接到远程MQTT失败: {rc}")

def on_remote_disconnect(client, userdata, rc):
    print("[INFO] 远程MQTT正常断开连接" if rc == 0 else f"[WARN] 远程MQTT意外断开连接 (rc={rc})，将自动重连...")

def init_mqtt_clients():
    global g_local_client, g_remote_client
    
    print("初始化本地MQTT客户端...")
    g_local_client = mqtt.Client(client_id="local_mqtt_subscriber_py_8975", clean_session=True)
    g_local_client.on_connect, g_local_client.on_disconnect, g_local_client.on_message = on_local_connect, on_local_disconnect, on_local_message
    try:
        g_local_client.connect(LOCAL_HOST, LOCAL_PORT, 60)
        g_local_client.loop_start()
        print(f"[SUCCESS] 本地MQTT客户端初始化成功 {LOCAL_HOST}:{LOCAL_PORT}")
    except Exception as e:
        print(f"[ERROR] 连接本地MQTT失败: {e}")
        return False
    
    print("初始化远程MQTT客户端...")
    g_remote_client = mqtt.Client(client_id="remote_mqtt_publisher_py", clean_session=True)
    g_remote_client.on_connect, g_remote_client.on_disconnect = on_remote_connect, on_remote_disconnect
    try:
        g_remote_client.connect(REMOTE_HOST, REMOTE_PORT, 60)
        g_remote_client.loop_start()
        print(f"[SUCCESS] 远程MQTT客户端初始化成功 {REMOTE_HOST}:{REMOTE_PORT}")
    except Exception as e:
        print(f"[ERROR] 连接远程MQTT失败: {e}")
        return False
    return True

def cleanup():
    global g_local_client, g_remote_client
    print("\n程序正在退出...")
    if g_local_client:
        g_local_client.loop_stop()
        g_local_client.disconnect()
    if g_remote_client:
        g_remote_client.loop_stop()
        g_remote_client.disconnect()
    print("[SUCCESS] 清理完成")

def signal_handler(signum, frame):
    global g_running
    g_running = False

def main():
    global g_vid, g_running
    
    print("="*50)
    print("本地MQTT到远程MQTT转发器")
    print(f"本地: {LOCAL_HOST}:{LOCAL_PORT} -> 远程: {REMOTE_HOST}:{REMOTE_PORT}")
    print("="*50)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    g_vid = read_vid_from_config()
    print(f"车辆ID: {g_vid}")
    
    init_logger("local_to_remote_mqtt_forwarder")
    
    if not init_mqtt_clients():
        cleanup()
        return 1
    
    time.sleep(1)
    print("\n开始转发MQTT消息...")
    for topic in FORWARD_TOPICS:
        print(f"  - {topic}")
    print("按 Ctrl+C 停止程序\n")
    
    try:
        while g_running:
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass
    
    cleanup()
    return 0

if __name__ == "__main__":
    sys.exit(main())