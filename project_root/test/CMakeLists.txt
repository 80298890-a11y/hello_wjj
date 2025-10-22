cmake_minimum_required(VERSION 3.16.3)
project(test_programs VERSION 1.0.0)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置FastDDS 2.6.10路径 - 使用实际安装路径
set(CMAKE_PREFIX_PATH "/usr/local" ${CMAKE_PREFIX_PATH})

# 查找依赖包 - FastDDS 2.6.10使用fastrtps包名
find_package(fastrtps REQUIRED)
find_package(fastcdr REQUIRED)

# 查找 spdlog
find_package(spdlog REQUIRED)

# 设置fastdds_lib路径
set(FASTDDS_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../fastdds_lib)

# 查找jsoncpp - 使用pkg-config方式
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP jsoncpp)

if(NOT JSONCPP_FOUND)
    # 备用方案：手动查找
    find_library(JSONCPP_LIBRARY NAMES jsoncpp)
    find_path(JSONCPP_INCLUDE_DIR NAMES json/json.h PATHS /usr/include /usr/local/include)
    if(JSONCPP_LIBRARY AND JSONCPP_INCLUDE_DIR)
        set(JSONCPP_LIBRARIES ${JSONCPP_LIBRARY})
        set(JSONCPP_INCLUDE_DIRS ${JSONCPP_INCLUDE_DIR})
    endif()
endif()

# 查找 yaml-cpp
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML_CPP yaml-cpp)
if(NOT YAML_CPP_FOUND)
    find_library(YAML_CPP_LIBRARY NAMES yaml-cpp)
    find_path(YAML_CPP_INCLUDE_DIR NAMES yaml-cpp/yaml.h PATHS /usr/include /usr/local/include)
    if(YAML_CPP_LIBRARY AND YAML_CPP_INCLUDE_DIR)
        set(YAML_CPP_LIBRARIES ${YAML_CPP_LIBRARY})
        set(YAML_CPP_INCLUDE_DIRS ${YAML_CPP_INCLUDE_DIR})
    endif()
endif()

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/Handshake
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/VehicleStatus
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/RemoteControl
    ${JSONCPP_INCLUDE_DIRS}
    ${YAML_CPP_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/logger_conf
)

# 编译通用日志系统库（基于 spdlog）
add_library(universal_logger STATIC logger_conf/universal_logger.cpp)
target_link_libraries(universal_logger 
    pthread
    ${YAML_CPP_LIBRARIES}
    stdc++fs
    spdlog::spdlog
)
target_include_directories(universal_logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 使用系统的 FastRTPS 库

# 编译publisher
add_executable(publisher publisher.cpp)
target_include_directories(publisher PRIVATE ${FASTDDS_LIB_DIR}/include)
target_link_libraries(publisher 
    ${FASTDDS_LIB_DIR}/build/libfastdds_lib.a
    fastrtps
    fastcdr
    pthread
    universal_logger
)

# 编译subscriber  
add_executable(subscriber subscriber.cpp)
target_include_directories(subscriber PRIVATE ${FASTDDS_LIB_DIR}/include)
target_link_libraries(subscriber
    ${FASTDDS_LIB_DIR}/build/libfastdds_lib.a
    fastrtps
    fastcdr
    pthread
    universal_logger
)

# 编译fastdds_to_mqtt_bridge
add_executable(fastdds_to_mqtt_bridge fastdds_to_mqtt_bridge.cpp)
target_include_directories(fastdds_to_mqtt_bridge PRIVATE ${FASTDDS_LIB_DIR}/include)
target_link_libraries(fastdds_to_mqtt_bridge
    ${FASTDDS_LIB_DIR}/build/libfastdds_lib.a
    fastrtps
    fastcdr
    pthread
    mosquitto
    universal_logger
)

# 编译mqtt_receiver
add_executable(mqtt_receiver mqtt_receiver.cpp)
target_link_libraries(mqtt_receiver
    pthread
    mosquitto
    yaml-cpp
    universal_logger
)

# 编译mqtt_sender (Direction 2)
add_executable(mqtt_sender mqtt_sender.cpp)
target_link_libraries(mqtt_sender
    pthread
    mosquitto
    universal_logger
)

# 编译mqtt_to_fastdds_bridge (Direction 2)
add_executable(mqtt_to_fastdds_bridge mqtt_to_fastdds_bridge.cpp)
target_include_directories(mqtt_to_fastdds_bridge PRIVATE ${FASTDDS_LIB_DIR}/include)
target_link_libraries(mqtt_to_fastdds_bridge
    ${FASTDDS_LIB_DIR}/build/libfastdds_lib.a
    fastrtps
    fastcdr
    pthread
    mosquitto
    ${JSONCPP_LIBRARIES}
    universal_logger
)

# 已删除的程序: tsp_forwarder, mqtt_simulator, data_processor - 由tsp_command_processor替代

# 编译tsp_command_processor (TSP命令处理器)
add_executable(tsp_command_processor tsp_command_processor.cpp)
target_link_libraries(tsp_command_processor
    pthread
    mosquitto
    stdc++fs
    ${JSONCPP_LIBRARIES}
    universal_logger
)

# test_action_stop已删除

# 编译local_to_remote_mqtt_forwarder (本地MQTT到远程MQTT转发器)
add_executable(local_to_remote_mqtt_forwarder local_to_remote_mqtt_forwarder.cpp)
target_link_libraries(local_to_remote_mqtt_forwarder
    pthread
    mosquitto
    universal_logger
)

# 编译bridge_forwarder (MQTT程序4)
add_executable(bridge_forwarder bridge_forwarder.cpp)
target_link_libraries(bridge_forwarder
    pthread
    mosquitto
    stdc++fs
    ${JSONCPP_LIBRARIES}
    universal_logger
)

# 编译vehicle_instruction_takeover (云舱接管功能处理器)
add_executable(vehicle_instruction_takeover vehicle_instruction_takeover.cpp)
target_link_libraries(vehicle_instruction_takeover
    pthread
    mosquitto
    ${JSONCPP_LIBRARIES}
    ${YAML_CPP_LIBRARIES}
    universal_logger
)

# 编译vehicle_instruction_braking (车辆刹车指令处理器)
add_executable(vehicle_instruction_braking vehicle_instruction_braking.cpp)
target_link_libraries(vehicle_instruction_braking
    pthread
    mosquitto
    ${JSONCPP_LIBRARIES}
    ${YAML_CPP_LIBRARIES}
    universal_logger
)

# 编译vehicle_instruction_lateral (车辆横向控制指令处理器)
add_executable(vehicle_instruction_lateral vehicle_instruction_lateral.cpp)
target_link_libraries(vehicle_instruction_lateral
    pthread
    mosquitto
    ${JSONCPP_LIBRARIES}
    ${YAML_CPP_LIBRARIES}
    universal_logger
)

# 编译vehicle_instruction_distance_time (车辆距离时间控制指令处理器)
add_executable(vehicle_instruction_distance_time vehicle_instruction_distance_time.cpp)
target_link_libraries(vehicle_instruction_distance_time
    pthread
    mosquitto
    ${JSONCPP_LIBRARIES}
    ${YAML_CPP_LIBRARIES}
    universal_logger
)

# 编译noa_info (TSP字段提取器)
add_executable(noa_info noa_info.cpp)
target_link_libraries(noa_info
    pthread
    mosquitto
    ${JSONCPP_LIBRARIES}
    ${YAML_CPP_LIBRARIES}
    universal_logger
)

# 编译vehicle_uplink_simulator (车端上行模拟器) - 文件不存在，暂时注释
# add_executable(vehicle_uplink_simulator vehicle_uplink_simulator.cpp)
# target_include_directories(vehicle_uplink_simulator PRIVATE ${FASTDDS_LIB_DIR}/include)
# target_link_libraries(vehicle_uplink_simulator
#     ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/Handshake/Handshake.cxx
#     ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/Handshake/HandshakePubSubTypes.cxx
#     ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/VehicleStatus/VehicleStatus.cxx
#     ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/VehicleStatus/VehicleStatusPubSubTypes.cxx
#     ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/RemoteControl/RemoteControl.cxx
#     ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/RemoteControl/RemoteControlPubSubTypes.cxx
#     fastrtps 
#     fastcdr
#     pthread
# )

