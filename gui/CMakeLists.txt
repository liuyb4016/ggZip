set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(ggzip_gui WIN32
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
