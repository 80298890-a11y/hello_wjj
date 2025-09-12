cmake_minimum_required(VERSION 3.16)
project(mqtt_fastdds_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用FastDDS 2.6.10
set(CMAKE_PREFIX_PATH "/usr/local/fastdds-2.6.10")

# 查找FastDDS 2.6.10包
find_package(fastrtps REQUIRED PATHS /usr/local/fastdds-2.6.10)

# 查找Mosquitto
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)

# 查找JSON
find_package(jsoncpp REQUIRED)

# 包含头文件目录
include_directories(../fastdds_lib/include)
include_directories(/usr/local/fastdds-2.6.10/include)

# 测试编译一个简单的程序
add_executable(fastdds_test_2610 ../mqtt_to_fastdds_bridge.cpp)

target_link_libraries(fastdds_test_2610 
    fastrtps 
    fastcdr
    ${MOSQUITTO_LIBRARIES}
    jsoncpp_lib
)

target_include_directories(fastdds_test_2610 PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
target_compile_options(fastdds_test_2610 PRIVATE ${MOSQUITTO_CFLAGS_OTHER})

# 添加库路径
link_directories(/usr/local/fastdds-2.6.10/lib)

