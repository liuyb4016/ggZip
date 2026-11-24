set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

set(GGZIP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../shell/res/ggzip.ico")
set(GGZIP_RESOURCE_DESCRIPTION "ggZip Archive Manager")
set(GGZIP_RESOURCE_FILENAME "ggzip_gui.exe")
set(GGZIP_RESOURCE_TYPE "0x1L")
set(GGZIP_MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/application.manifest")
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
    src/LanguageManager.h src/LanguageManager.cpp
    src/TaskQueue.h src/TaskQueue.cpp
    src/BatchDialog.h src/BatchDialog.cpp
    src/UpdateDialog.h src/UpdateDialog.cpp
)
target_include_directories(ggzip_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ../core/src)
target_link_libraries(ggzip_gui PRIVATE ggzip_core Qt6::Widgets Qt6::Network)

if(MSVC)
    target_link_options(ggzip_gui PRIVATE /MANIFEST:NO)
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
    add_executable(ggzip_image_resource_benchmark ../tests/image_resource_benchmark.cpp src/PreviewImage.cpp)
    target_include_directories(ggzip_image_resource_benchmark PRIVATE src ../tests ../core/src)
    target_link_libraries(ggzip_image_resource_benchmark PRIVATE ggzip_core Qt6::Gui)
    target_compile_definitions(ggzip_image_resource_benchmark PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_dependencies(ggzip_image_resource_benchmark ggzip_preview_worker)
    if(MSVC)
        target_compile_options(ggzip_image_resource_benchmark PRIVATE /W4 /utf-8 /permissive-)
    endif()
    add_test(NAME image_resource_benchmark COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{SystemRoot}/System32;$ENV{SystemRoot}"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
        $<TARGET_FILE:ggzip_image_resource_benchmark>)
    set_tests_properties(image_resource_benchmark PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
    add_executable(ggzip_desktop_flow_test ../tests/desktop_flow_test.cpp resources.qrc
        src/MainWindow.cpp src/ArchiveTableModel.cpp src/Dialogs.cpp src/Preferences.cpp
        src/ProgressRunner.cpp src/PreviewSession.cpp src/PreviewImage.cpp src/TaskQueue.cpp
        src/BatchDialog.cpp src/UpdateDialog.cpp)
    target_include_directories(ggzip_desktop_flow_test PRIVATE src ../tests ../core/src)
    target_link_libraries(ggzip_desktop_flow_test PRIVATE ggzip_core Qt6::Widgets Qt6::Network)
    if(MSVC)
        target_compile_options(ggzip_desktop_flow_test PRIVATE /W4 /utf-8 /permissive-)
    endif()
    add_test(NAME desktop_flows COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{SystemRoot}/System32;$ENV{SystemRoot}"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
        "QT_QPA_PLATFORM=offscreen" $<TARGET_FILE:ggzip_desktop_flow_test>)
    set_tests_properties(desktop_flows PROPERTIES TIMEOUT 60)
    add_executable(ggzip_task_queue_test ../tests/task_queue_test.cpp src/TaskQueue.cpp
        src/ProgressRunner.cpp src/UpdateDialog.cpp)
    target_include_directories(ggzip_task_queue_test PRIVATE src)
    target_link_libraries(ggzip_task_queue_test PRIVATE ggzip_core Qt6::Widgets Qt6::Network)
    if(MSVC)
        target_compile_options(ggzip_task_queue_test PRIVATE /W4 /utf-8 /permissive-)
    endif()
    add_test(NAME task_queue_update COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{SystemRoot}/System32;$ENV{SystemRoot}"
        $<TARGET_FILE:ggzip_task_queue_test>)
    set_tests_properties(task_queue_update PROPERTIES TIMEOUT 30)
    add_executable(ggzip_gui_regression_test ../tests/gui_regression_test.cpp
        resources.qrc src/LanguageManager.cpp
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
