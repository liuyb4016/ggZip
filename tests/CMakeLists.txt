add_executable(ggzip_smoke smoke_test.cpp)
target_link_libraries(ggzip_smoke PRIVATE ggzip_core)

if(MSVC)
    target_compile_options(ggzip_smoke PRIVATE /W4 /utf-8 /permissive-)
elseif(MINGW)
    target_compile_options(ggzip_smoke PRIVATE -municode -Wall -Wextra)
    target_link_options(ggzip_smoke PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()
