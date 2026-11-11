cmake_minimum_required(VERSION 3.10)
project(chat_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 把 ../sdk 作为子目录引入，产出 ai_chat_sdk 静态库
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../sdk ${CMAKE_BINARY_DIR}/sdk_build)

find_package(Threads REQUIRED)

add_executable(chat_server
    src/main.cpp
    src/chatserver.cpp)

target_include_directories(chat_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(chat_server PRIVATE
    ai_chat_sdk
    gflags
    Threads::Threads)

# 把静态页面拷贝到可执行文件旁边，保证在 build 目录直接运行也能访问页面
add_custom_command(TARGET chat_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/static
        $<TARGET_FILE_DIR:chat_server>/static)
