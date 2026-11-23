set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(GGZIP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../shell/res/ggzip.ico")
set(GGZIP_RESOURCE_DESCRIPTION "ggZip Archive Manager")
set(GGZIP_RESOURCE_FILENAME "ggzip_gui.exe")
set(GGZIP_RESOURCE_TYPE "0x1L")
configure_file(../cmake/windows.rc.in "${CMAKE_CURRENT_BINARY_DIR}/ggzip_gui.rc" @ONLY)

qt_add_executable(ggzip_gui WIN32
    "${CMAKE_CURRENT_BINARY_DIR}/ggzip_gui.rc"
    resources.qrc
    src/main.cpp
    src/MainWindow.h     src/MainWindow.cpp
    src/ArchiveTableModel.h  src/ArchiveTableModel.cpp
    src/Dialogs.h        src/Dialogs.cpp
    src/ProgressRunner.h src/ProgressRunner.cpp
)
target_include_directories(ggzip_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ggzip_gui PRIVATE ggzip_core Qt6::Widgets)

if(MSVC)
    target_compile_options(ggzip_gui PRIVATE /W4 /utf-8 /permissive-)
elseif(MINGW)
    target_compile_options(ggzip_gui PRIVATE -Wall -Wextra)
    target_link_options(ggzip_gui PRIVATE -static -static-libgcc -static-libstdc++)
endif()
