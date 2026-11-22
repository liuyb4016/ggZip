add_library(ggzip_core STATIC
    src/Text.cpp
    src/Format.cpp
    src/ProcessUtil.cpp
    src/SevenZipEngine.cpp
    src/EngineFactory.cpp
    src/SmartExtract.cpp
)
target_include_directories(ggzip_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(ggzip_core PUBLIC UNICODE _UNICODE)
target_compile_definitions(ggzip_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(ggzip_core PRIVATE /W4 /utf-8 /permissive-)
elseif(MINGW)
    target_compile_options(ggzip_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
