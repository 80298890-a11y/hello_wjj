cmake_minimum_required(VERSION 3.16)
project(mqtt_cpp_sender)

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖库
find_package(PkgConfig REQUIRED)

# 查找jsoncpp
find_library(JSONCPP_LIBRARY jsoncpp)
find_path(JSONCPP_INCLUDE_DIR json/json.h
    PATHS
    /usr/include/jsoncpp
    /usr/local/include/jsoncpp
    /usr/include
    /usr/local/include
)

# 查找mosquitto
find_library(MOSQUITTO_LIBRARY mosquitto)
find_path(MOSQUITTO_INCLUDE_DIR mosquitto.h
    PATHS
    /usr/include
    /usr/local/include
)

if(NOT JSONCPP_LIBRARY OR NOT JSONCPP_INCLUDE_DIR)
    message(FATAL_ERROR "未找到 jsoncpp 库，请安装: sudo apt-get install libjsoncpp-dev")
endif()

if(NOT MOSQUITTO_LIBRARY OR NOT MOSQUITTO_INCLUDE_DIR)
    message(FATAL_ERROR "未找到 mosquitto 库，请安装: sudo apt-get install libmosquitto-dev")
endif()

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${JSONCPP_INCLUDE_DIR}
    ${MOSQUITTO_INCLUDE_DIR}
)

# 创建可执行文件
add_executable(mqtt_sender
    mqtt_sender.cpp
)

add_executable(mqtt_receiver
    mqtt_receiver.cpp
)

# 链接库
target_link_libraries(mqtt_sender 
    ${JSONCPP_LIBRARY}
    ${MOSQUITTO_LIBRARY}
)

target_link_libraries(mqtt_receiver 
    ${JSONCPP_LIBRARY}
    ${MOSQUITTO_LIBRARY}
)

# 安装
install(TARGETS mqtt_sender mqtt_receiver
    RUNTIME DESTINATION bin
) 