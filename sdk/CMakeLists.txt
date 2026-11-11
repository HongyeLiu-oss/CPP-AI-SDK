cmake_minimum_required(VERSION 3.10)
project(ai_chat_sdk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDK 编译为静态库，供 server / test 链接
add_library(ai_chat_sdk STATIC
    src/chatsdk.cpp
    src/authmanager.cpp
    src/llmmanager.cpp
    src/sessionmanager.cpp
    src/datamanager.cpp
    src/deepseekprovider.cpp
    src/glmprovider.cpp
    src/util/log.cpp
    src/util/crypto.cpp)

target_include_directories(ai_chat_sdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# cpp-httplib 需要 OpenSSL 支持才能访问 https 接口
target_compile_definitions(ai_chat_sdk PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

target_link_libraries(ai_chat_sdk PUBLIC
    jsoncpp
    spdlog
    fmt
    sqlite3
    cpp-httplib
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto)
