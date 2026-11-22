# ggZip Shell 扩展 —— 独立构建（不并入根工程，避免与主程序耦合）
# 用法：
#   cmake -S shell -B build-shell -A x64        （MSVC / Visual Studio 生成器）
#   cmake --build build-shell --config Release
#   ctest --test-dir build-shell -C Release     （或直接运行 shell_host_test，见下）
cmake_minimum_required(VERSION 3.20)
project(ggZipShell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ggzip_shell SHARED
    src/dllmain.cpp
    src/ClassFactory.cpp
    src/ShellMenu.cpp
    src/RegistryUtil.cpp
    src/Launcher.cpp
    ggzip_shell.rc
    src/ggzip_shell.def
)

set_target_properties(ggzip_shell PROPERTIES
    OUTPUT_NAME "ggzip_shell"
    PREFIX ""
)

# 中文菜单文本需要 /utf-8
target_compile_options(ggzip_shell PRIVATE /utf-8)
target_link_libraries(ggzip_shell PRIVATE shell32 shlwapi user32 advapi32)

# 测试宿主：不经 Explorer 验证注册/菜单/干跑命令行
add_executable(shell_host_test test/host_test.cpp)
target_compile_options(shell_host_test PRIVATE /utf-8)
target_link_libraries(shell_host_test PRIVATE ole32 shell32 user32 advapi32)
