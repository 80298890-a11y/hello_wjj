cmake_minimum_required(VERSION 3.16)
project(j6_uplink_simulator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用FastDDS 2.6.10
set(CMAKE_PREFIX_PATH "/usr/local/fastdds-2.6.10")

# 查找FastDDS 2.6.10包
find_package(fastrtps REQUIRED PATHS /usr/local/fastdds-2.6.10)

# 包含FastDDS 2.6.10头文件目录
include_directories(/usr/local/fastdds-2.6.10/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../shared/include)

# 添加库路径
link_directories(/usr/local/fastdds-2.6.10/lib)

# 设置RPATH，这样就不需要手动设置LD_LIBRARY_PATH
set(CMAKE_INSTALL_RPATH "/usr/local/fastdds-2.6.10/lib:/usr/local/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# 收集所有源文件
file(GLOB_RECURSE SOURCE_FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx
)

# 创建车机上行模拟器可执行文件
add_executable(vehicle_uplink_simulator 
    src/vehicle_uplink_simulator.cpp
    src/Handshake.cxx
    src/HandshakePubSubTypes.cxx
    src/RemoteControl.cxx
    src/RemoteControlPubSubTypes.cxx
    src/VehicleStatus.cxx
    src/VehicleStatusPubSubTypes.cxx
)

target_link_libraries(vehicle_uplink_simulator 
    fastrtps 
    fastcdr
    pthread
)

# 创建J6消息接收器可执行文件
add_executable(j6_message_receiver 
    src/j6_message_receiver.cpp
    ../shared/src/AsyncLogger.cpp
    src/Handshake.cxx
    src/HandshakePubSubTypes.cxx
    src/RemoteControl.cxx
    src/RemoteControlPubSubTypes.cxx
    src/VehicleStatus.cxx
    src/VehicleStatusPubSubTypes.cxx
)

target_link_libraries(j6_message_receiver 
    fastrtps 
    fastcdr
    pthread
)

# 输出信息
message(STATUS "=== J6 上行系统 (FastRTPS 2.6.10) ===")
message(STATUS "车机模拟器: ./vehicle_uplink_simulator")
message(STATUS "消息接收器: ./j6_message_receiver")
message(STATUS "发送频率: 100Hz")
message(STATUS "FastDDS 路径: /usr/local/fastdds-2.6.10")
