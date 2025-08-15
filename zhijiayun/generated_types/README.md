# Generated Types

## 目录说明

本目录包含由IDL文件自动生成的FastDDS类型定义文件。

### 文件结构

```
generated_types/
├── README.md           # 本说明文档
├── Handshake/          # 握手消息类型
│   ├── Handshake.hpp                    # 握手消息结构定义
│   ├── HandshakeCdrAux.hpp             # CDR序列化辅助
│   ├── HandshakeCdrAux.ipp             # CDR序列化实现
│   ├── HandshakePubSubTypes.cxx        # 发布订阅类型实现
│   ├── HandshakePubSubTypes.hpp        # 发布订阅类型头文件
│   ├── HandshakeTypeObjectSupport.cxx  # 类型对象支持实现
│   └── HandshakeTypeObjectSupport.hpp  # 类型对象支持头文件
├── VehicleStatus/      # 车辆状态消息类型
│   ├── VehicleStatus.hpp                    # 车辆状态消息结构定义
│   ├── VehicleStatusCdrAux.hpp             # CDR序列化辅助
│   ├── VehicleStatusCdrAux.ipp             # CDR序列化实现
│   ├── VehicleStatusPubSubTypes.cxx        # 发布订阅类型实现
│   ├── VehicleStatusPubSubTypes.hpp        # 发布订阅类型头文件
│   ├── VehicleStatusTypeObjectSupport.cxx  # 类型对象支持实现
│   └── VehicleStatusTypeObjectSupport.hpp  # 类型对象支持头文件
└── RemoteControl/      # 远程控制消息类型
    ├── RemoteControl.hpp                    # 远程控制消息结构定义
    ├── RemoteControlCdrAux.hpp             # CDR序列化辅助
    ├── RemoteControlCdrAux.ipp             # CDR序列化实现
    ├── RemoteControlPubSubTypes.cxx        # 发布订阅类型实现
    ├── RemoteControlPubSubTypes.hpp        # 发布订阅类型头文件
    ├── RemoteControlTypeObjectSupport.cxx  # 类型对象支持实现
    └── RemoteControlTypeObjectSupport.hpp  # 类型对象支持头文件
```

### 文件说明

1. **结构定义文件** (*.hpp)
   - 包含C++类定义，对应IDL中的struct
   - 定义消息的数据结构和访问方法

2. **CDR序列化文件** (*CdrAux.hpp/.ipp)
   - CDR (Common Data Representation) 序列化支持
   - 用于网络传输时的数据编码/解码

3. **发布订阅类型文件** (*PubSubTypes.cxx/.hpp)
   - FastDDS发布订阅系统所需的类型注册信息
   - 包含类型名称、序列化方法等

4. **类型对象支持文件** (*TypeObjectSupport.cxx/.hpp)
   - FastDDS类型发现和动态类型支持
   - 用于运行时类型信息查询

### 生成方式

这些文件由fastddsgen工具从IDL文件自动生成：

```bash
# 对于每个IDL文件，执行：
fastddsgen -replace -d generated_types/{TypeName}/ idl/{TypeName}.idl
```

### 重要注意事项

- ⚠️ **请勿手动修改本目录下的任何文件**
- ⚠️ **所有文件都是自动生成的，修改会被覆盖**
- ✅ 如需修改消息结构，请编辑对应的IDL文件，然后重新生成
- ✅ 生成过程由CMake自动处理，无需手动执行

### 扩展方式

添加新的消息类型：

1. 在`idl/`目录中创建新的`.idl`文件
2. 在`fastdds_lib/CMakeLists.txt`中的`IDL_FILES`列表中添加新文件
3. 重新编译项目，新的类型文件会自动生成到对应目录

### 依赖关系

- **输入**：`idl/`目录中的IDL文件
- **工具**：fastddsgen (FastDDS代码生成器)
- **输出**：本目录中的C++类型定义文件
- **使用者**：`fastdds_lib`中的发布者和订阅者代码
