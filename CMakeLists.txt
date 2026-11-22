cmake_minimum_required(VERSION 3.20)
project(ggZip VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GGZ_BUILD_TESTS "构建冒烟测试" ON)
option(GGZ_BUILD_GUI   "构建 Qt GUI（需要 Qt6，v0.2 起启用）" OFF)

add_subdirectory(core)
add_subdirectory(cli)

if(GGZ_BUILD_TESTS)
    add_subdirectory(tests)
endif()
