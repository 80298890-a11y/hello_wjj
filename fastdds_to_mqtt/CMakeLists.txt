cmake_minimum_required(VERSION 3.12)
project(fastdds_mqtt_bridge)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖包
find_package(fastdds REQUIRED)
# 查找jsoncpp
find_path(JSONCPP_INCLUDE_DIR json/json.h
    PATHS /usr/include/jsoncpp /usr/local/include/jsoncpp)
if(NOT JSONCPP_INCLUDE_DIR)
    message(FATAL_ERROR "jsoncpp headers not found")
endif()

find_library(JSONCPP_LIBRARY NAMES jsoncpp)
if(NOT JSONCPP_LIBRARY)
    message(FATAL_ERROR "jsoncpp library not found")
endif()

# 查找 mosquitto 库
find_library(MOSQUITTO_LIBRARY NAMES mosquitto)
if(NOT MOSQUITTO_LIBRARY)
    message(FATAL_ERROR "mosquitto library not found")
endif()

find_path(MOSQUITTO_INCLUDE_DIR NAMES mosquitto.h
    PATHS /usr/include /usr/local/include)
if(NOT MOSQUITTO_INCLUDE_DIR)
    message(FATAL_ERROR "mosquitto.h not found")
endif()

# 添加CloudToVehicle库
add_library(CloudToVehicle STATIC
    CloudToVehiclePubSubTypes.cxx
    CloudToVehicleTypeObjectSupport.cxx
)

# 设置包含目录
target_include_directories(CloudToVehicle PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# 编译发布者
add_executable(fastdds_publisher
    fastdds_publisher.cpp
)

target_include_directories(fastdds_publisher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(fastdds_publisher
    fastdds
    CloudToVehicle
)

# 编译订阅者（带MQTT桥接）
add_executable(fastdds_subscriber
    fastdds_subscriber.cpp
)

target_include_directories(fastdds_subscriber PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${MOSQUITTO_INCLUDE_DIR}
    ${JSONCPP_INCLUDE_DIR}
)

target_link_libraries(fastdds_subscriber
    fastdds
    CloudToVehicle
    ${MOSQUITTO_LIBRARY}
    ${JSONCPP_LIBRARY}
)