add_library(ggzip_core STATIC
    src/Text.cpp
    src/Format.cpp
    src/ProcessUtil.cpp
    src/SevenZipEngine.cpp
    src/EngineFactory.cpp
    src/SmartExtract.cpp
    src/PasswordStore.cpp
    src/FileAssociations.cpp
    src/ArchivePath.cpp
    src/FileTransaction.cpp
    src/SourceMark.cpp
)
target_include_directories(ggzip_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(ggzip_core PUBLIC UNICODE _UNICODE)
target_compile_definitions(ggzip_core PUBLIC GGZIP_VERSION="${PROJECT_VERSION}")
target_compile_definitions(ggzip_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    target_compile_options(ggzip_core PRIVATE /W4 /utf-8 /permissive-)
elseif(MINGW)
    target_compile_options(ggzip_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# DPAPI（密码管理器）
target_link_libraries(ggzip_core PRIVATE crypt32 advapi32 shell32 bcrypt)
