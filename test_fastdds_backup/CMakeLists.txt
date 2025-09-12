cmake_minimum_required(VERSION 3.12)
project(test_fastdds)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置查找路径 - 使用相对路径和标准路径
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH})

# 查找依赖包
find_package(fastdds REQUIRED)

# 查找 mosquitto 库
find_library(MOSQUITTO_LIBRARY NAMES mosquitto)
find_path(MOSQUITTO_INCLUDE_DIR NAMES mosquitto.h
    PATHS /usr/include /usr/local/include)

if(MOSQUITTO_LIBRARY AND MOSQUITTO_INCLUDE_DIR)
    message(STATUS "✅ 找到libmosquitto，将使用标准MQTT库")
    set(HAVE_MOSQUITTO TRUE)
    add_compile_definitions(HAVE_MOSQUITTO)
else()
    message(STATUS "⚠️ libmosquitto未找到，将使用自实现MQTT客户端")
    set(HAVE_MOSQUITTO FALSE)
    set(MOSQUITTO_LIBRARY "")
    set(MOSQUITTO_INCLUDE_DIR "")
endif()

# 查找fastddsgen工具 - 让CMake自动在系统PATH中查找
find_program(FASTDDSGEN fastddsgen)
if(NOT FASTDDSGEN)
    message(FATAL_ERROR "fastddsgen not found")
endif()

# 定义生成IDL的函数 - 修正生成的文件列表
function(generate_idl IDL_FILE)
    get_filename_component(IDL_NAME ${IDL_FILE} NAME_WE)
    
    set(GENERATED_FILES
        ${CMAKE_CURRENT_BINARY_DIR}/${IDL_NAME}.hpp
        ${CMAKE_CURRENT_BINARY_DIR}/${IDL_NAME}PubSubTypes.cxx
        ${CMAKE_CURRENT_BINARY_DIR}/${IDL_NAME}PubSubTypes.hpp
        ${CMAKE_CURRENT_BINARY_DIR}/${IDL_NAME}TypeObjectSupport.cxx
        ${CMAKE_CURRENT_BINARY_DIR}/${IDL_NAME}TypeObjectSupport.hpp
        ${CMAKE_CURRENT_BINARY_DIR}/${IDL_NAME}CdrAux.hpp
        ${CMAKE_CURRENT_BINARY_DIR}/${IDL_NAME}CdrAux.ipp
    )
    
    add_custom_command(
        OUTPUT ${GENERATED_FILES}
        COMMAND ${FASTDDSGEN} -cs -d ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${IDL_FILE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${IDL_FILE}
        COMMENT "Generating C++ code from ${IDL_FILE}"
    )
    
    add_custom_target(${IDL_NAME}_generation DEPENDS ${GENERATED_FILES})
endfunction()

# 生成IDL文件
generate_idl(Handshake.idl)
generate_idl(VehicleStatus.idl)
generate_idl(RemoteControl.idl)

# HandShake Publisher - 移除不存在的.cxx文件
add_executable(handshake_publisher 
    handshake_publisher.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/HandshakePubSubTypes.cxx
    ${CMAKE_CURRENT_BINARY_DIR}/HandshakeTypeObjectSupport.cxx)
add_dependencies(handshake_publisher Handshake_generation)
target_include_directories(handshake_publisher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(handshake_publisher fastdds)

# HandShake Subscriber (包含MQTT桥接功能)
add_executable(handshake_subscriber 
    handshake_subscriber.cpp
    mqtt_mosquitto.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/HandshakePubSubTypes.cxx
    ${CMAKE_CURRENT_BINARY_DIR}/HandshakeTypeObjectSupport.cxx)
add_dependencies(handshake_subscriber Handshake_generation)
target_include_directories(handshake_subscriber PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(handshake_subscriber fastdds mosquitto)

# VehicleStatus Publisher
add_executable(vehicle_status_publisher 
    vehicle_status_publisher.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/VehicleStatusPubSubTypes.cxx
    ${CMAKE_CURRENT_BINARY_DIR}/VehicleStatusTypeObjectSupport.cxx)
add_dependencies(vehicle_status_publisher VehicleStatus_generation)
target_include_directories(vehicle_status_publisher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(vehicle_status_publisher fastdds)

# VehicleStatus Subscriber (包含MQTT桥接功能)
add_executable(vehicle_status_subscriber 
    vehicle_status_subscriber.cpp
    mqtt_mosquitto.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/VehicleStatusPubSubTypes.cxx
    ${CMAKE_CURRENT_BINARY_DIR}/VehicleStatusTypeObjectSupport.cxx)
add_dependencies(vehicle_status_subscriber VehicleStatus_generation)
target_include_directories(vehicle_status_subscriber PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(vehicle_status_subscriber fastdds mosquitto)

# RemoteControl Publisher
add_executable(remote_control_publisher 
    remote_control_publisher.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/RemoteControlPubSubTypes.cxx
    ${CMAKE_CURRENT_BINARY_DIR}/RemoteControlTypeObjectSupport.cxx)
add_dependencies(remote_control_publisher RemoteControl_generation)
target_include_directories(remote_control_publisher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(remote_control_publisher fastdds)

# RemoteControl Subscriber (包含MQTT桥接功能)
add_executable(remote_control_subscriber 
    remote_control_subscriber.cpp
    mqtt_mosquitto.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/RemoteControlPubSubTypes.cxx
    ${CMAKE_CURRENT_BINARY_DIR}/RemoteControlTypeObjectSupport.cxx)
add_dependencies(remote_control_subscriber RemoteControl_generation)
target_include_directories(remote_control_subscriber PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(remote_control_subscriber fastdds mosquitto)



