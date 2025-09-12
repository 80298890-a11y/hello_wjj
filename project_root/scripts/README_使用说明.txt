=== Supervisor自启动配置使用说明 ===

📁 文件说明：
/home/hello/work/project_root/scripts/
├── 8803_dds_to_mqtt.conf           # Supervisor配置文件
├── quick_test_commands.txt         # 快速测试验证命令
├── supervisor_setup_commands.txt   # 完整安装配置命令
└── README_使用说明.txt            # 本说明文件

🚀 使用步骤：

1️⃣ 首先执行快速测试（可选但推荐）
   打开 quick_test_commands.txt
   逐行复制粘贴执行命令，确保环境准备就绪

2️⃣ 执行正式安装配置  
   打开 supervisor_setup_commands.txt
   按顺序执行 "=== FastDDS到MQTT桥接服务 Supervisor安装命令清单 ===" 部分的命令

3️⃣ 验证安装结果
   执行：sudo supervisorctl status
   应该看到：
   fastdds_to_mqtt_bridge    RUNNING   pid xxx, uptime x:xx:xx
   subscriber               RUNNING   pid xxx, uptime x:xx:xx

✅ 成功标志：
- 两个服务都显示 RUNNING 状态
- fastdds_to_mqtt_bridge 优先启动 (priority=10)
- subscriber 后续启动 (priority=20)
- 系统重启后自动启动服务

📋 日常管理：
使用 supervisor_setup_commands.txt 中的 "=== 日常管理命令 ===" 部分

🔧 故障排查：
使用 supervisor_setup_commands.txt 中的 "=== 故障排查命令 ===" 部分

🗑️ 卸载清理：
使用 supervisor_setup_commands.txt 中的 "=== 卸载清理命令 ===" 部分

💡 提示：
- 所有命令都可以直接复制粘贴执行
- 建议逐条执行，观察输出结果
- 如有错误，参考故障排查部分

