# CMakeLists.txt for timestamp_diff project
cmake_minimum_required(VERSION 3.20)

# 定义项目
project(timestamp_diff VERSION 1 LANGUAGES CXX)

# 设置原始项目目录路径
set(ORIGINAL_PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cloud_to_vehicle_dds)

# 查找必要的库
if(NOT fastcdr_FOUND)
    find_package(fastcdr 2 REQUIRED)
endif()

if(NOT fastdds_FOUND)
    find_package(fastdds 3 REQUIRED)
endif()

# 设置默认构建类型为Release
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 设置需要的源文件
set(DDS_SOURCES
    ${ORIGINAL_PROJECT_DIR}/CloudToVehiclePubSubTypes.cxx
    ${ORIGINAL_PROJECT_DIR}/CloudToVehicleTypeObjectSupport.cxx
    VehicleAckPubSubTypes.cxx
    VehicleAckTypeObjectSupport.cxx
)

# 创建转发器可执行文件
add_executable(vehicle_ack_forwarder
    vehicle_ack_forwarder.cpp
    ${DDS_SOURCES}
)

# 创建接收器可执行文件
add_executable(vehicle_ack_receiver
    vehicle_ack_receiver.cpp
    ${DDS_SOURCES}
)

# 创建集成测试器可执行文件
add_executable(integrated_tester
    integrated_tester.cpp
    ${DDS_SOURCES}
)

# 为可执行文件添加编译定义
target_compile_definitions(vehicle_ack_forwarder PRIVATE
    $<$<AND:$<NOT:$<BOOL:${WIN32}>>,$<STREQUAL:"${CMAKE_BUILD_TYPE}","Debug">>:_DEBUG>
    $<$<AND:$<BOOL:${WIN32}>,$<STREQUAL:"${CMAKE_BUILD_TYPE}","Debug">>:_DEBUG>
    $<$<AND:$<NOT:$<BOOL:${WIN32}>>,$<STREQUAL:"${CMAKE_BUILD_TYPE}","Release">>:_RELEASE>
    $<$<AND:$<BOOL:${WIN32}>,$<STREQUAL:"${CMAKE_BUILD_TYPE}","Release">>:_RELEASE>
)

target_compile_definitions(vehicle_ack_receiver PRIVATE
    $<$<AND:$<NOT:$<BOOL:${WIN32}>>,$<STREQUAL:"${CMAKE_BUILD_TYPE}","Debug">>:_DEBUG>
    $<$<AND:$<BOOL:${WIN32}>,$<STREQUAL:"${CMAKE_BUILD_TYPE}","Debug">>:_DEBUG>
    $<$<AND:$<NOT:$<BOOL:${WIN32}>>,$<STREQUAL:"${CMAKE_BUILD_TYPE}","Release">>:_RELEASE>
    $<$<AND:$<BOOL:${WIN32}>,$<STREQUAL:"${CMAKE_BUILD_TYPE}","Release">>:_RELEASE>
)

target_compile_definitions(integrated_tester PRIVATE
    $<$<AND:$<NOT:$<BOOL:${WIN32}>>,$<STREQUAL:"${CMAKE_BUILD_TYPE}","Debug">>:_DEBUG>
    $<$<AND:$<BOOL:${WIN32}>,$<STREQUAL:"${CMAKE_BUILD_TYPE}","Debug">>:_DEBUG>
    $<$<AND:$<NOT:$<BOOL:${WIN32}>>,$<STREQUAL:"${CMAKE_BUILD_TYPE}","Release">>:_RELEASE>
    $<$<AND:$<BOOL:${WIN32}>,$<STREQUAL:"${CMAKE_BUILD_TYPE}","Release">>:_RELEASE>
)

# 添加头文件搜索路径
target_include_directories(vehicle_ack_forwarder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ORIGINAL_PROJECT_DIR}
)

target_include_directories(vehicle_ack_receiver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ORIGINAL_PROJECT_DIR}
)

target_include_directories(integrated_tester PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ORIGINAL_PROJECT_DIR}
)

# 链接库
target_link_libraries(vehicle_ack_forwarder
    fastcdr
    fastdds
)

target_link_libraries(vehicle_ack_receiver
    fastcdr
    fastdds
)

target_link_libraries(integrated_tester
    fastcdr
    fastdds
)

# 设置编译器标志
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vehicle_ack_forwarder PRIVATE -std=c++11)
    target_compile_options(vehicle_ack_receiver PRIVATE -std=c++11)
    target_compile_options(integrated_tester PRIVATE -std=c++11)
endif()

if(MSVC)
    target_compile_options(vehicle_ack_forwarder PRIVATE /std:c++11)
    target_compile_options(vehicle_ack_receiver PRIVATE /std:c++11)
    target_compile_options(integrated_tester PRIVATE /std:c++11)
endif()