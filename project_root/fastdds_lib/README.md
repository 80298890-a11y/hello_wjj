# FastDDS Library

## 目录说明

本目录为FastDDS核心基础库，提供**通用的FastDDS功能**，不包含具体的业务逻辑。

### 文件结构

```
fastdds_lib/
├── README.md           # 本说明文档
├── CMakeLists.txt      # CMake构建配置
├── include/            # 头文件目录
│   ├── fastdds_publisher.hpp     # FastDDS发布器基类
│   ├── fastdds_subscriber.hpp    # FastDDS订阅器基类
│   ├── fastdds_mqtt_publisher.hpp # FastDDS+MQTT集成发布器
│   └── mqtt_mosquitto.hpp        # MQTT客户端封装
├── src/                # 源文件目录
│   ├── fastdds_publisher.cpp     # FastDDS发布器实现
│   ├── fastdds_subscriber.cpp    # FastDDS订阅器实现
│   ├── fastdds_mqtt_publisher.cpp # FastDDS+MQTT集成发布器实现
│   └── mqtt_mosquitto.cpp        # MQTT客户端实现
└── build/              # 构建输出目录
    └── libfastdds_lib.a       # 编译生成的静态库
```

### 基础库说明

#### 1. FastDDSPublisher - FastDDS发布器基类
- **功能**：提供FastDDS消息发送的通用接口
- **支持的消息类型**：
  - HandshakeRequest (`/handshake/request`) - 握手请求
  - HandshakeResponse (`/handshake/response`) - 握手响应  
  - VehicleStatus (`/vehicle/vehicle_status`) - 车辆状态
  - RemoteControl (`/vehicle/control_cmd`) - 远程控制
- **特点**：
  - 虚拟接口设计，支持继承扩展
  - 自动处理FastDDS初始化和清理
  - 禁用数据共享避免内存问题
  - 包含IDL定义的所有字段
  - **专注生产环境必要功能**，代码简洁高效

#### 2. FastDDSSubscriber - FastDDS订阅器基类
- **功能**：提供FastDDS消息接收的通用接口
- **支持的消息类型**：与FastDDSPublisher一致（4个生产环境必要主题）
- **特点**：
  - 回调函数机制处理接收到的消息
  - 自动订阅所有支持的topic
  - 线程安全的消息处理
  - 支持自定义消息处理逻辑
  - **专注生产环境必要功能**，无冗余代码

#### 3. FastDDSMQTTPublisher - FastDDS+MQTT集成发布器
- **功能**：继承FastDDSPublisher，同时支持MQTT发布
- **特点**：
  - 继承复用FastDDS发布功能
  - 集成MQTT发布能力
  - 支持同时发送FastDDS和MQTT消息

#### 4. MQTTToFastDDSConverter - MQTT到FastDDS转换器
- **功能**：将MQTT消息转换为FastDDS消息
- **支持的MQTT主题**：
  - `/handshake/request` → HandshakeRequest
  - `/handshake/response` → HandshakeResponse
  - `/vehicle/vehicle_status` → VehicleStatus
  - `/vehicle/control_cmd` → RemoteControl
  
- **特点**：
  - 自动JSON解析和类型转换
  - 完整的错误处理机制
  - 高效的消息处理流程

#### 5. MQTTMosquitto - MQTT客户端封装
- **功能**：封装libmosquitto库，提供简单的MQTT接口
- **特点**：
  - 异步发布机制
  - 自动连接管理
  - 简化的API接口

### 设计原则

#### 1. 基础库定位
- **只提供通用功能**：不包含具体的业务逻辑
- **接口导向**：提供虚拟接口供业务程序继承和实现
- **可复用性**：可以被多个不同的业务程序使用

#### 2. 与业务逻辑分离
- **基础库** (`fastdds_lib/`) - 通用FastDDS功能
- **业务逻辑** (`test/`) - 具体的业务实现和逻辑

#### 3. 扩展性设计
- 虚拟函数支持继承扩展
- 回调函数支持自定义处理逻辑
- 模块化设计便于组合使用

### 编译方式

```bash
cd fastdds_lib/build
cmake ..
make
```

编译后会生成：
- `libfastdds_lib.a` - 静态库文件

### 使用方式

#### 在业务程序中使用

**CMakeLists.txt配置**：
```cmake
# 包含目录
include_directories(
    ${FASTDDS_LIB_DIR}/include
    ${FASTDDS_LIB_DIR}/build
    # ... IDL生成的头文件目录
)

# 链接静态库
link_directories(${FASTDDS_LIB_DIR}/build)
target_link_libraries(your_program 
    fastdds_lib
    fastdds
    pthread
)
```

**在C++代码中使用**：
```cpp
#include "../fastdds_lib/include/fastdds_publisher.hpp"

int main() {
    // 创建发布器
    auto publisher = std::make_unique<FastDDSPublisher>(0);
    
    // 初始化
    if (!publisher->init()) {
        return -1;
    }
    
    // 发送消息
    publisher->publishVehicleStatus("VehicleStatusTopic", 
        12345, 60.5, 1, 116.397, 39.916);
    
    return 0;
}
```

### IDL类型支持

基础库自动支持以下IDL类型：
- **Handshake.idl** - HandshakeRequest, HandshakeResponse
- **VehicleStatus.idl** - VehicleStatus  
- **RemoteControl.idl** - ControlCmd

#### 🧹 代码简化优化 (2025-01-20)

**清理内容**：
- ✅ 移除了不必要的TSP相关处理代码（约200行代码）
- ✅ 专注于4个生产环境必要主题的处理
- ✅ 提升编译效率和运行性能
- ✅ 代码更简洁易维护

**保留功能**：
- ✅ 完整的HandshakeRequest/Response处理
- ✅ 完整的VehicleStatus处理  
- ✅ 完整的RemoteControl处理
- ✅ MQTT to FastDDS转换功能

#### 添加新IDL类型

1. **在idl/目录添加新的.idl文件**
2. **更新CMakeLists.txt的IDL_FILES列表**：
   ```cmake
   set(IDL_FILES
       ${IDL_SOURCE_DIR}/Handshake.idl
       ${IDL_SOURCE_DIR}/VehicleStatus.idl
       ${IDL_SOURCE_DIR}/RemoteControl.idl
       ${IDL_SOURCE_DIR}/NewMessage.idl  # 新增
   )
   ```
3. **在fastdds_publisher.hpp中添加新的虚拟接口**：
   ```cpp
   virtual bool publishNewMessage(
       const std::string& topic,
       // ... 参数列表
   );
   ```
4. **在fastdds_publisher.cpp中实现接口**
5. **在fastdds_subscriber.hpp中添加回调类型和设置方法**
6. **在fastdds_subscriber.cpp中实现订阅逻辑**

### 依赖关系

**编译依赖**：
- FastDDS
- libmosquitto-dev
- CMake 3.16+
- C++17编译器

**运行时依赖**：
- FastDDS运行时库
- libmosquitto

**输入依赖**：
- IDL文件 (`../idl/`)
- 生成的类型文件 (`../generated_types/`)

**使用者**：
- 业务逻辑程序 (`../test/`)

### 开发指导

#### 扩展发布器功能

```cpp
// 在fastdds_publisher.hpp中添加
class FastDDSPublisher {
public:
    // 添加新的虚拟接口
    virtual bool publishCustomMessage(
        const std::string& topic,
        const CustomData& data
    );
};

// 在fastdds_publisher.cpp中实现
bool FastDDSPublisher::publishCustomMessage(
    const std::string& topic,
    const CustomData& data) {
    
    // 创建类型支持
    TypeSupport type_support(new CustomDataPubSubType());
    
    // 获取或创建主题
    Topic* topic_ptr = getOrCreateTopic(topic, type_support.get_type_name(), type_support);
    if (!topic_ptr) return false;
    
    // 获取或创建数据写入器
    DataWriter* writer = getOrCreateDataWriter(topic, topic_ptr);
    if (!writer) return false;
    
    // 创建并发送消息
    CustomData msg = data;
    return writer->write(&msg) == RETCODE_OK;
}
```

#### 扩展订阅器功能

```cpp
// 在fastdds_subscriber.hpp中添加
class FastDDSSubscriber {
public:
    using CustomDataCallback = std::function<void(const CustomData&)>;
    void setCustomDataCallback(CustomDataCallback callback);
    bool subscribeCustomData(const std::string& topic);
    
private:
    CustomDataCallback custom_data_callback_;
};

// 实现回调处理和订阅逻辑
```

### 注意事项

- ⚠️ **基础库职责**：只提供通用功能，不实现具体业务逻辑
- ✅ **接口稳定性**：虚拟接口设计保证向后兼容
- ✅ **线程安全**：内部处理了多线程访问安全
- ✅ **资源管理**：自动处理FastDDS资源的创建和清理
- ✅ **错误处理**：提供完整的错误返回和日志记录

### 相关文档

- IDL文件定义：`../idl/README.md`
- 生成的类型文件：`../generated_types/README.md`  
- 业务逻辑实现：`../test/README.md`