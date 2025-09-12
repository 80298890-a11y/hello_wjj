# IDL Files

## 目录说明

本目录包含接口定义语言(IDL)文件，用于定义FastDDS消息的数据结构。

### 文件结构

```
idl/
├── README.md           # 本说明文档
├── Handshake.idl       # 握手消息定义
├── VehicleStatus.idl   # 车辆状态消息定义
└── RemoteControl.idl   # 远程控制消息定义
```

### IDL文件说明

#### 1. Handshake.idl - 握手消息
定义系统间的握手协议消息：
- `HandshakeRequest` - 握手请求
- `HandshakeResponse` - 握手响应

**主要字段：**
- `noa_active_request/noa_active` - NOA激活请求/状态
- `remote_override_status/override_response` - 远程覆盖状态/响应
- `remote_override_ready/control_source` - 远程覆盖就绪/控制源

#### 2. VehicleStatus.idl - 车辆状态
定义车辆当前状态信息：

**主要字段：**
- `vehicle_id` - 车辆ID
- `control_mode` - 控制模式
- `longitude/latitude` - 经纬度坐标
- `timestamp` - 时间戳

#### 3. RemoteControl.idl - 远程控制
定义远程控制命令：

**主要字段：**
- `steering_angle_enable/steering_angle` - 转向角使能/角度
- `target_acceleration_enable/target_acceleration` - 加速度使能/值
- `indicator_left/right_enable` - 转向灯使能
- `gear_position_enable/gear_position` - 档位使能/位置
- `ebrake_status_enable/ebrake_status` - 手刹使能/状态

### 设计原则

1. **模块化**：每个IDL文件定义独立的功能模块
2. **扩展性**：使用`@extensibility(APPENDABLE)`支持向后兼容的字段添加
3. **命名空间**：所有类型都在`remote_msgs`命名空间下
4. **字段完整性**：每个消息包含完整的业务字段，不遗漏不冗余

### 修改流程

当需要修改消息结构时：

1. **编辑IDL文件**
   ```bash
   # 编辑对应的IDL文件
   vim idl/VehicleStatus.idl
   ```

2. **重新生成类型文件**
   ```bash
   # 重新编译项目，CMake会自动调用fastddsgen重新生成
   cd fastdds_lib/build
   make
   ```

3. **更新业务代码**
   - 在`fastdds_lib/src/`中更新发布者/订阅者代码
   - 在`test/`中更新测试程序，添加新字段的处理

4. **验证兼容性**
   - 确保新字段有合理的默认值
   - 测试与旧版本的兼容性

### 添加新消息类型

1. **创建新IDL文件**
   ```bash
   # 例如添加GPS消息
   vim idl/GPS.idl
   ```

2. **更新CMake配置**
   ```cmake
   # 在fastdds_lib/CMakeLists.txt中添加
   set(IDL_FILES
       # ... 现有文件 ...
       ${IDL_SOURCE_DIR}/GPS.idl
   )
   ```

3. **更新库代码**
   - 在`fastdds_lib/src/fastdds_publisher.cpp`中添加发布方法
   - 在`fastdds_lib/src/fastdds_subscriber.cpp`中添加订阅方法
   - 在头文件中添加对应的接口声明

4. **更新测试代码**
   - 在`test/`中添加新消息类型的发送/接收逻辑

### 注意事项

- ⚠️ **向后兼容**：修改现有字段可能破坏兼容性
- ✅ **添加字段**：在结构体末尾添加新字段是安全的
- ✅ **字段命名**：使用清晰、一致的命名规范
- ✅ **数据类型**：选择合适的数据类型（float, double, long等）
- ✅ **默认值**：为新字段考虑合理的默认值

### 相关文档

- [FastDDS IDL语法参考](https://fast-dds.docs.eprosima.com/en/latest/fastdds/dds_layer/topic/typeSupport/typeSupport.html)
- [OMG IDL规范](https://www.omg.org/spec/IDL)
- FastDDS类型生成：`generated_types/README.md`
