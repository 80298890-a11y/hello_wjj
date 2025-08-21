cmake_minimum_required(VERSION 3.16)
project(j6_downlink_system)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用FastDDS 2.6.10
set(CMAKE_PREFIX_PATH "/usr/local/fastdds-2.6.10")

# 查找FastDDS 2.6.10包
find_package(fastrtps REQUIRED PATHS /usr/local/fastdds-2.6.10)

# 查找jsoncpp
find_library(JSONCPP_LIBRARY NAMES jsoncpp)
find_path(JSONCPP_INCLUDE_DIR NAMES json/json.h PATHS /usr/include/jsoncpp)

# 包含FastDDS 2.6.10头文件目录
include_directories(/usr/local/fastdds-2.6.10/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../shared/include)
include_directories(${JSONCPP_INCLUDE_DIR})

# 添加库路径
link_directories(/usr/local/fastdds-2.6.10/lib)

# 设置RPATH，这样就不需要手动设置LD_LIBRARY_PATH
set(CMAKE_INSTALL_RPATH "/usr/local/fastdds-2.6.10/lib:/usr/local/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# 创建云端MQTT发送器
add_executable(cloud_mqtt_sender 
    src/cloud_mqtt_sender.cpp
    ../shared/src/AsyncLogger.cpp
    ../shared/src/ConfigManager.cpp
)

target_link_libraries(cloud_mqtt_sender 
    mosquitto
    pthread
    ${JSONCPP_LIBRARY}
)

# 创建MQTT到FastDDS桥接器
add_executable(mqtt_to_fastdds_bridge 
    src/mqtt_to_fastdds_bridge.cpp
    ../shared/src/AsyncLogger.cpp
    ../shared/src/ConfigManager.cpp
    src/Handshake.cxx
    src/HandshakePubSubTypes.cxx
    src/RemoteControl.cxx
    src/RemoteControlPubSubTypes.cxx
    src/VehicleStatus.cxx
    src/VehicleStatusPubSubTypes.cxx
)

target_link_libraries(mqtt_to_fastdds_bridge 
    fastrtps 
    fastcdr
    mosquitto
    pthread
    ${JSONCPP_LIBRARY}
)

# 创建车机接收器
add_executable(vehicle_receiver 
    src/vehicle_receiver.cpp
    ../shared/src/AsyncLogger.cpp
    ../shared/src/ConfigManager.cpp
    src/Handshake.cxx
    src/HandshakePubSubTypes.cxx
    src/RemoteControl.cxx
    src/RemoteControlPubSubTypes.cxx
    src/VehicleStatus.cxx
    src/VehicleStatusPubSubTypes.cxx
)

target_link_libraries(vehicle_receiver 
    fastrtps 
    fastcdr
    pthread
    ${JSONCPP_LIBRARY}
)

# 输出信息
message(STATUS "=== J6 下行系统 (FastRTPS 2.6.10) ===")
message(STATUS "云端MQTT发送器: ./cloud_mqtt_sender")
message(STATUS "MQTT到FastDDS桥接器: ./mqtt_to_fastdds_bridge")
message(STATUS "车机接收器: ./vehicle_receiver")
message(STATUS "配置文件: config/downlink_config.json")
message(STATUS "FastDDS 路径: /usr/local/fastdds-2.6.10")
