cmake_minimum_required(VERSION 3.16)
project(fastdds_helloworld_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用FastDDS 2.6.10
set(CMAKE_PREFIX_PATH "/usr/local/fastdds-2.6.10")

# 查找FastDDS 2.6.10包
find_package(fastrtps REQUIRED PATHS /usr/local/fastdds-2.6.10)

# 包含FastDDS 2.6.10头文件目录
include_directories(/usr/local/fastdds-2.6.10/include)

# 添加库路径
link_directories(/usr/local/fastdds-2.6.10/lib)

# 创建发布者可执行文件 (纯FastRTPS 2.6.10)
add_executable(publisher HelloWorldPublisher.cpp)
target_link_libraries(publisher 
    fastrtps 
    fastcdr
)

# 创建订阅者可执行文件 (纯FastRTPS 2.6.10)
add_executable(subscriber HelloWorldSubscriber.cpp)
target_link_libraries(subscriber 
    fastrtps 
    fastcdr
)

# 兼容B机器的发布者
add_executable(fastrtps_publisher build/fastrtps_publisher.cpp)
target_link_libraries(fastrtps_publisher 
    fastrtps 
    fastcdr
)

# 输出信息
message(STATUS "=== FastDDS 2.6.10 HelloWorld Demo ===")
message(STATUS "Publisher: ./publisher [samples]")
message(STATUS "Subscriber: ./subscriber [samples]")
message(STATUS "FastDDS 路径: /usr/local/fastdds-2.6.10")

