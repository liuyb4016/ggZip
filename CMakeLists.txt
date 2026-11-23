cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GGZIP_VERSION LIMIT_COUNT 1)
project(ggZip VERSION ${GGZIP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GGZ_BUILD_TESTS "构建冒烟测试" ON)
option(GGZ_BUILD_GUI   "构建 Qt GUI（需要 Qt6，通过 CMAKE_PREFIX_PATH 指定 Qt 目录）" OFF)

add_subdirectory(core)
add_subdirectory(cli)

if(GGZ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(GGZ_BUILD_GUI)
    add_subdirectory(gui)
endif()
