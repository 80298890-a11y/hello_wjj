cmake_minimum_required(VERSION 3.16.3)
project(test_programs VERSION 1.0.0)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖包
find_package(fastdds REQUIRED)

# 设置fastdds_lib路径
set(FASTDDS_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../fastdds_lib)

# 查找jsoncpp
find_library(JSONCPP_LIBRARY NAMES jsoncpp)
find_path(JSONCPP_INCLUDE_DIR NAMES json/json.h PATHS /usr/include/jsoncpp)

# 包含目录
include_directories(
    ${FASTDDS_LIB_DIR}/include
    ${FASTDDS_LIB_DIR}/build
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/Handshake
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/VehicleStatus
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/RemoteControl
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types/TSPCommand
    ${JSONCPP_INCLUDE_DIR}
)

# 链接fastdds_lib静态库
link_directories(${FASTDDS_LIB_DIR}/build)

# 编译publisher
add_executable(publisher publisher.cpp)
target_link_libraries(publisher 
    fastdds_lib
    fastdds
    pthread
)

# 编译subscriber  
add_executable(subscriber subscriber.cpp)
target_link_libraries(subscriber
    fastdds_lib
    fastdds
    pthread
)

# 编译fastdds_to_mqtt_bridge
add_executable(fastdds_to_mqtt_bridge fastdds_to_mqtt_bridge.cpp)
target_link_libraries(fastdds_to_mqtt_bridge
    fastdds_lib
    fastdds
    pthread
    mosquitto
)

# 编译mqtt_receiver
add_executable(mqtt_receiver mqtt_receiver.cpp)
target_link_libraries(mqtt_receiver
    pthread
    mosquitto
)

# 编译mqtt_sender (Direction 2)
add_executable(mqtt_sender mqtt_sender.cpp)
target_link_libraries(mqtt_sender
    pthread
    mosquitto
)

# 编译mqtt_to_fastdds_bridge (Direction 2)
add_executable(mqtt_to_fastdds_bridge mqtt_to_fastdds_bridge.cpp)
target_link_libraries(mqtt_to_fastdds_bridge
    fastdds_lib
    fastdds
    pthread
    mosquitto
    ${JSONCPP_LIBRARY}
)

# 编译tsp_forwarder (MQTT程序1)
add_executable(tsp_forwarder tsp_forwarder.cpp)
target_link_libraries(tsp_forwarder
    pthread
    mosquitto
    stdc++fs
    ${JSONCPP_LIBRARY}
)

# 编译mqtt_simulator (MQTT程序X)
add_executable(mqtt_simulator mqtt_simulator.cpp)
target_link_libraries(mqtt_simulator
    pthread
    mosquitto
    stdc++fs
    ${JSONCPP_LIBRARY}
)

# 编译data_processor (MQTT程序2)
add_executable(data_processor data_processor.cpp)
target_link_libraries(data_processor
    pthread
    mosquitto
    stdc++fs
    ${JSONCPP_LIBRARY}
)

# test_action_stop已删除

# 编译bridge_forwarder (MQTT程序4)
add_executable(bridge_forwarder bridge_forwarder.cpp)
target_link_libraries(bridge_forwarder
    pthread
    mosquitto
    stdc++fs
    ${JSONCPP_LIBRARY}
)

