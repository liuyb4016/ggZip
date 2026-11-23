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
    src/PreviewSession.h src/PreviewSession.cpp
    src/PreviewImage.h src/PreviewImage.cpp
    src/Preferences.h src/Preferences.cpp
)
target_include_directories(ggzip_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ../core/src)
target_link_libraries(ggzip_gui PRIVATE ggzip_core Qt6::Widgets)

if(MSVC)
    target_compile_options(ggzip_gui PRIVATE /W4 /utf-8 /permissive-)
elseif(MINGW)
    target_compile_options(ggzip_gui PRIVATE -Wall -Wextra)
    target_link_options(ggzip_gui PRIVATE -static -static-libgcc -static-libstdc++)
endif()

add_executable(ggzip_preview_worker src/PreviewDecoder.cpp)
target_link_libraries(ggzip_preview_worker PRIVATE Qt6::Gui)
if(MSVC)
    target_compile_options(ggzip_preview_worker PRIVATE /W4 /utf-8)
endif()

if(GGZ_BUILD_TESTS)
    add_executable(ggzip_gui_regression_test ../tests/gui_regression_test.cpp
        src/ArchiveTableModel.cpp src/Dialogs.cpp src/PreviewImage.cpp src/ProgressRunner.cpp)
    target_include_directories(ggzip_gui_regression_test PRIVATE src ../core/src ../tests)
    target_link_libraries(ggzip_gui_regression_test PRIVATE ggzip_core Qt6::Widgets)
    add_dependencies(ggzip_gui_regression_test ggzip_preview_worker)
    if(MSVC)
        target_compile_options(ggzip_gui_regression_test PRIVATE /W4 /utf-8 /permissive-)
    endif()
    add_test(NAME gui_regression COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{SystemRoot}/System32;$ENV{SystemRoot}"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
        "QT_QPA_PLATFORM=offscreen" $<TARGET_FILE:ggzip_gui_regression_test>)
    set_tests_properties(gui_regression PROPERTIES TIMEOUT 60)
    add_executable(ggzip_preview_session_test
        ../tests/preview_session_test.cpp src/PreviewSession.cpp)
    target_include_directories(ggzip_preview_session_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(ggzip_preview_session_test PRIVATE ggzip_core Qt6::Core)
    if(MSVC)
        target_compile_options(ggzip_preview_session_test PRIVATE /W4 /utf-8 /permissive-)
    elseif(MINGW)
        target_compile_options(ggzip_preview_session_test PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME preview_session COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{SystemRoot}/System32;$ENV{SystemRoot}"
        $<TARGET_FILE:ggzip_preview_session_test>)
endif()
