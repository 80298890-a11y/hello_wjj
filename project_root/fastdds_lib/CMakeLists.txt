cmake_minimum_required(VERSION 3.16.3)
project(fastdds_lib VERSION 1.0.0)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置FastDDS 2.6.10路径
set(CMAKE_PREFIX_PATH "/usr/local" ${CMAKE_PREFIX_PATH})

# 查找依赖包 - 使用FastDDS 2.6.10
find_package(fastrtps REQUIRED)
find_package(fastcdr REQUIRED)

# 查找libmosquitto
find_library(MOSQUITTO_LIBRARY NAMES mosquitto)
find_path(MOSQUITTO_INCLUDE_DIR NAMES mosquitto.h)

# 查找jsoncpp
find_library(JSONCPP_LIBRARY NAMES jsoncpp)
find_path(JSONCPP_INCLUDE_DIR NAMES json/json.h PATHS /usr/include/jsoncpp)

if(NOT MOSQUITTO_LIBRARY OR NOT MOSQUITTO_INCLUDE_DIR)
    message(FATAL_ERROR "libmosquitto not found. Please install libmosquitto-dev")
endif()

if(NOT JSONCPP_LIBRARY OR NOT JSONCPP_INCLUDE_DIR)
    message(FATAL_ERROR "jsoncpp not found. Please install libjsoncpp-dev")
endif()

# 查找fastddsgen工具
find_program(FASTDDSGEN fastddsgen PATHS /usr/local/bin)
if(NOT FASTDDSGEN)
    message(FATAL_ERROR "fastddsgen not found")
endif()

# 获取IDL文件路径（从idl目录）
set(IDL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../idl)

# IDL文件列表
set(IDL_FILES
    ${IDL_SOURCE_DIR}/HandShake.idl
    ${IDL_SOURCE_DIR}/VehicleStatus.idl
    ${IDL_SOURCE_DIR}/RemoteControl.idl
    ${IDL_SOURCE_DIR}/DriveState.idl
    ${IDL_SOURCE_DIR}/FaultReport.idl
)

# 设置生成文件的基础输出目录
set(GENERATED_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../generated_types)

# 生成的源文件列表（按IDL分目录） - FastDDS 2.6.10 不生成TypeObjectSupport
set(GENERATED_SOURCES
    ${GENERATED_BASE_DIR}/HandShake/HandShake.cxx
    ${GENERATED_BASE_DIR}/HandShake/HandShakePubSubTypes.cxx
    ${GENERATED_BASE_DIR}/VehicleStatus/VehicleStatus.cxx
    ${GENERATED_BASE_DIR}/VehicleStatus/VehicleStatusPubSubTypes.cxx
    ${GENERATED_BASE_DIR}/RemoteControl/RemoteControl.cxx
    ${GENERATED_BASE_DIR}/RemoteControl/RemoteControlPubSubTypes.cxx
    ${GENERATED_BASE_DIR}/DriveState/DriveState.cxx
    ${GENERATED_BASE_DIR}/DriveState/DriveStatePubSubTypes.cxx
    ${GENERATED_BASE_DIR}/FaultReport/FaultReport.cxx
    ${GENERATED_BASE_DIR}/FaultReport/FaultReportPubSubTypes.cxx
)

# 为每个IDL文件添加生成规则
foreach(IDL_FILE ${IDL_FILES})
    get_filename_component(IDL_NAME ${IDL_FILE} NAME_WE)
    
    # 为每个IDL创建独立的输出目录
    set(IDL_OUTPUT_DIR ${GENERATED_BASE_DIR}/${IDL_NAME})
    
    add_custom_command(
        OUTPUT 
            ${IDL_OUTPUT_DIR}/${IDL_NAME}.cxx
            ${IDL_OUTPUT_DIR}/${IDL_NAME}PubSubTypes.cxx
            ${IDL_OUTPUT_DIR}/${IDL_NAME}.h
            ${IDL_OUTPUT_DIR}/${IDL_NAME}PubSubTypes.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${IDL_OUTPUT_DIR}
        COMMAND ${FASTDDSGEN} -replace -d ${IDL_OUTPUT_DIR} ${IDL_FILE}
        DEPENDS ${IDL_FILE}
        COMMENT "Generating FastDDS types for ${IDL_NAME}.idl to generated_types/${IDL_NAME}/"
    )
    
    # 创建生成目标
    add_custom_target(${IDL_NAME}_generation DEPENDS
        ${IDL_OUTPUT_DIR}/${IDL_NAME}.cxx
        ${IDL_OUTPUT_DIR}/${IDL_NAME}PubSubTypes.cxx
        ${IDL_OUTPUT_DIR}/${IDL_NAME}.h
        ${IDL_OUTPUT_DIR}/${IDL_NAME}PubSubTypes.h
    )
endforeach()

# 创建静态库 (包含发布者和订阅者)
add_library(fastdds_lib STATIC
    src/fastdds_publisher.cpp
    src/fastdds_mqtt_publisher.cpp
    src/fastdds_subscriber.cpp
    src/mqtt_mosquitto.cpp
    src/mqtt_to_fastdds_converter.cpp
    ${GENERATED_SOURCES}
)

# 添加依赖关系
add_dependencies(fastdds_lib 
    HandShake_generation 
    VehicleStatus_generation 
    RemoteControl_generation
    DriveState_generation
    FaultReport_generation
)

# 设置包含目录（包含所有IDL子目录）
target_include_directories(fastdds_lib 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${GENERATED_BASE_DIR}/HandShake>
        $<BUILD_INTERFACE:${GENERATED_BASE_DIR}/VehicleStatus>
        $<BUILD_INTERFACE:${GENERATED_BASE_DIR}/RemoteControl>
        $<BUILD_INTERFACE:${GENERATED_BASE_DIR}/DriveState>
        $<BUILD_INTERFACE:${GENERATED_BASE_DIR}/FaultReport>
        $<BUILD_INTERFACE:${JSONCPP_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src
)

# 添加FastDDS 2.6.10包含目录
target_include_directories(fastdds_lib 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${GENERATED_BASE_DIR}/HandShake>
        $<BUILD_INTERFACE:${GENERATED_BASE_DIR}/VehicleStatus>
        $<BUILD_INTERFACE:${GENERATED_BASE_DIR}/RemoteControl>
        $<BUILD_INTERFACE:${GENERATED_BASE_DIR}/DriveState>
        $<BUILD_INTERFACE:${GENERATED_BASE_DIR}/FaultReport>
        $<BUILD_INTERFACE:${JSONCPP_INCLUDE_DIR}>
        $<BUILD_INTERFACE:/usr/local/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src
)

# 链接库
target_link_libraries(fastdds_lib 
    PUBLIC 
        fastrtps
        fastcdr
        ${MOSQUITTO_LIBRARY}
        ${JSONCPP_LIBRARY}
)

# 设置库的属性
set_target_properties(fastdds_lib PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# 注意：这是一个基础库，业务逻辑程序在test目录中实现
