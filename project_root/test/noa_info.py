#!/usr/bin/env python3
"""TSP字段提取器: 从云端MQTT提取字段并发布到noa/info/{vid}"""

import json
import yaml
import logging
from logging.handlers import RotatingFileHandler
import paho.mqtt.client as mqtt
import signal
import sys
import time
from pathlib import Path

# 全局变量
g_running = True
g_vid = ""
g_client = None

# 提取的字段
class ExtractedFields:
    def __init__(self):
        self.control_mode = -999
        self.current_control_source = False
        self.noa_active_response = -999
        self.timestamp = 0
    
    def __eq__(self, other):
        return (self.control_mode == other.control_mode and
                self.current_control_source == other.current_control_source and
                self.noa_active_response == other.noa_active_response)

g_current = ExtractedFields()
g_last_published = ExtractedFields()

# 配置
MQTT_HOST = "hellorobotaxi.cn"
MQTT_PORT = 11883

# 日志
topic_logger = None
main_logger = None

def get_current_timestamp():
    return int(time.time() * 1000)

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
        'programs': {'noa_info': {'subdir': 'noa_info', 'level': 'INFO', 'max_files': 15}}
    }

def init_logger(program_name="noa_info"):
    global topic_logger, main_logger
    
    log_config = read_log_config()
    base_dir = log_config.get('global', {}).get('base_dir', 'build/logs')
    program_config = log_config.get('programs', {}).get(program_name, {})
    subdir = program_config.get('subdir', 'noa_info')
    level = program_config.get('level', 'INFO')
    max_files = program_config.get('max_files', 15)
    
    log_dir = Path(base_dir) / subdir
    log_dir.mkdir(parents=True, exist_ok=True)
    
    # 主日志
    main_logger = logging.getLogger('main')
    main_logger.setLevel(getattr(logging, level))
    console_handler = logging.StreamHandler()
    console_handler.setFormatter(logging.Formatter('[%(levelname)s] %(message)s'))
    main_logger.addHandler(console_handler)
    
    # topic日志
    topic_logger = logging.getLogger('topic')
    topic_logger.setLevel(logging.INFO)
    topic_logger.propagate = False
    log_file = log_dir / "noa_info.log"
    file_handler = RotatingFileHandler(log_file, maxBytes=5*1024*1024, backupCount=max_files)
    file_handler.setFormatter(logging.Formatter('[%(asctime)s] %(message)s', datefmt='%Y-%m-%d %H:%M:%S'))
    topic_logger.addHandler(file_handler)
    
    print(f"[SUCCESS] 日志系统初始化完成，日志目录: {log_dir.absolute()}")

def has_changed():
    return not (g_current == g_last_published)

def publish_if_changed():
    global g_last_published
    
    if not has_changed():
        return
    
    payload_dict = {
        "control_mode": g_current.control_mode,
        "current_control_source": g_current.current_control_source,
        "noa_active_response": g_current.noa_active_response,
        "timestamp": g_current.timestamp,
        "published_timestamp": get_current_timestamp()
    }
    
    payload = json.dumps(payload_dict, ensure_ascii=False)
    topic = f"noa/info/{g_vid}"
    
    try:
        result = g_client.publish(topic, payload, qos=0)
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            g_last_published = ExtractedFields()
            g_last_published.control_mode = g_current.control_mode
            g_last_published.current_control_source = g_current.current_control_source
            g_last_published.noa_active_response = g_current.noa_active_response
            g_last_published.timestamp = g_current.timestamp
            
            print(f"✅ [{topic}] {payload}")
            topic_logger.info(f"[{topic}] {payload}")
    except Exception as e:
        main_logger.error(f"发布失败: {e}")

def process_vehicle_status(root):
    if 'control_mode' in root:
        old = g_current.control_mode
        g_current.control_mode = root['control_mode']
        g_current.timestamp = root.get('received_timestamp', get_current_timestamp())
        
        if old != g_current.control_mode:
            print(f"control_mode: {old} → {g_current.control_mode}")
            publish_if_changed()

def process_handshake_response(root):
    changed = False
    
    # 调试：打印收到的JSON内容
    json_str = json.dumps(root, ensure_ascii=False)
    print(f"[DEBUG] 收到handshake/response: {json_str}")
    
    if 'current_control_source' in root:
        old = g_current.current_control_source
        new_value = bool(root['current_control_source'])
        print(f"[DEBUG] current_control_source字段存在，old={old}, new={new_value}")
        g_current.current_control_source = new_value
        if old != g_current.current_control_source:
            print(f"current_control_source: {old} → {g_current.current_control_source}")
            changed = True
    else:
        print("[DEBUG] current_control_source字段不存在！")
    
    if 'noa_active_response' in root:
        old = g_current.noa_active_response
        g_current.noa_active_response = root['noa_active_response']
        g_current.timestamp = root.get('received_timestamp', get_current_timestamp())
        if old != g_current.noa_active_response:
            print(f"noa_active_response: {old} → {g_current.noa_active_response}")
            changed = True
    
    if changed:
        publish_if_changed()

def on_message(client, userdata, msg):
    try:
        topic = msg.topic
        payload = msg.payload.decode('utf-8')
        
        try:
            data = json.loads(payload)
        except json.JSONDecodeError as e:
            main_logger.error(f"JSON解析失败: {e}, payload: {payload}")
            return
        
        vehicle_status_topic = f"/vehicle/vehicle_status/{g_vid}"
        handshake_topic = f"/handshake/response/{g_vid}"
        
        if topic == vehicle_status_topic:
            process_vehicle_status(data)
        elif topic == handshake_topic:
            process_handshake_response(data)
    except Exception as e:
        main_logger.error(f"处理消息失败: {e}")

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        vehicle_status_topic = f"/vehicle/vehicle_status/{g_vid}"
        handshake_topic = f"/handshake/response/{g_vid}"
        client.subscribe(vehicle_status_topic, qos=0)
        client.subscribe(handshake_topic, qos=0)
        print("✅ 已订阅云端MQTT")
    else:
        print(f"❌ 连接失败: {rc}")

def on_disconnect(client, userdata, rc):
    if rc == 0:
        print("[INFO] MQTT正常断开连接")
    else:
        print(f"[WARN] MQTT意外断开连接 (rc={rc})，将自动重连...")

def init_mqtt_client():
    global g_client
    
    g_client = mqtt.Client(client_id="tsp_field_extractor", clean_session=True)
    g_client.on_connect = on_connect
    g_client.on_disconnect = on_disconnect
    g_client.on_message = on_message
    
    try:
        g_client.connect(MQTT_HOST, MQTT_PORT, 60)
        g_client.loop_start()
        return True
    except Exception as e:
        print(f"❌ 连接失败: {e}")
        return False

def cleanup():
    global g_client
    if g_client:
        g_client.loop_stop()
        g_client.disconnect()
    print("[SUCCESS] 清理完成")

def signal_handler(signum, frame):
    global g_running
    g_running = False

def main():
    global g_vid, g_running
    
    print("="*50)
    print("TSP字段提取器")
    print("="*50)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    g_vid = read_vid_from_config()
    print(f"VID: {g_vid}")
    print(f"订阅: /vehicle/vehicle_status/{g_vid}, /handshake/response/{g_vid}")
    print(f"发布: noa/info/{g_vid}")
    
    init_logger("noa_info")
    
    if not init_mqtt_client():
        cleanup()
        return 1
    
    time.sleep(1)
    print("\n开始监听消息...")
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

