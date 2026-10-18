# 设置CMake的最小版本
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(testLLM)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# cpp-httplib 需要 OpenSSL 支持
add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)

# 设置构建类型Debug
set(CMAKE_BUILD_TYPE Debug)

# 添加可执行文件
add_executable(testLLM testllm.cpp
    ../sdk/src/util/log.cpp
    ../sdk/src/deepseekprovider.cpp)

# 设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/build)

# 添加头文件搜索路径
include_directories(${CMAKE_PROJECT_INCLUDE_DIR}/../sdk/include)

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

# 链接库
target_link_libraries(testLLM jsoncpp fmt spdlog gtest cpp-httplib pthread OpenSSL::SSL OpenSSL::Crypto)
