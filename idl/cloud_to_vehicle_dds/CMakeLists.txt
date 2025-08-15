# CMakeLists.txt - CloudToVehicle 项目构建配置
# 功能：构建 Topic 1 的发布者和订阅者程序

# === CMake 版本要求 ===
cmake_minimum_required(VERSION 3.20)

# === 项目定义 ===
project(cloud_to_vehicle VERSION 1 LANGUAGES CXX)

# === 查找依赖库 ===
# 查找 FastCDR 库（序列化库）
if(NOT fastcdr_FOUND)
    find_package(fastcdr 2 REQUIRED)
endif()

# 查找 FastDDS 库（DDS 核心库）
if(NOT fastdds_FOUND)
    find_package(fastdds 3 REQUIRED)
endif()

# === 编译器检查 ===
include(CheckCXXCompilerFlag)

# 检查 C++11 支持
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    check_cxx_compiler_flag(-std=c++11 SUPPORTS_CXX11)
    if(NOT SUPPORTS_CXX11)
        message(FATAL_ERROR "编译器不支持 C++11")
    endif()
endif()

# === 设置默认构建类型 ===
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "设置默认构建类型为 Release")
    set(CMAKE_BUILD_TYPE Release)
endif()

# === 收集源文件 ===
# 收集所有 FastDDS 生成的源文件
file(GLOB DDS_SOURCES 
    "CloudToVehicle*.cxx"
)

# === 创建发布者可执行文件 ===
add_executable(cloud_to_vehicle_publisher
    publisher.cpp          # 我们的发布者代码
    ${DDS_SOURCES}         # FastDDS 生成的代码
)

# === 创建订阅者可执行文件 ===
add_executable(cloud_to_vehicle_subscriber
    subscriber.cpp         # 我们的订阅者代码
    ${DDS_SOURCES}         # FastDDS 生成的代码
)

# === 设置包含目录 ===
# 为发布者设置包含目录
target_include_directories(cloud_to_vehicle_publisher PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}  # 当前目录，用于查找头文件
)

# 为订阅者设置包含目录
target_include_directories(cloud_to_vehicle_subscriber PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}  # 当前目录，用于查找头文件
)

# === 链接库 ===
# 为发布者链接必要的库
target_link_libraries(cloud_to_vehicle_publisher 
    fastdds    # FastDDS 核心库
    fastcdr    # 序列化库
)

# 为订阅者链接必要的库
target_link_libraries(cloud_to_vehicle_subscriber 
    fastdds    # FastDDS 核心库
    fastcdr    # 序列化库
)

# === 构建信息输出 ===
message(STATUS "构建配置完成")
message(STATUS "将生成以下可执行文件：")
message(STATUS "  - cloud_to_vehicle_publisher  (发布者)")
message(STATUS "  - cloud_to_vehicle_subscriber (订阅者)")