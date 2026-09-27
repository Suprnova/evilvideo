cmake_minimum_required(VERSION 3.25)
project(evilvideo VERSION 0.1.0 LANGUAGES C RC)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

configure_file(src/version.h.in version.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN)

if(MSVC)
    add_compile_options(/W4 /utf-8)
    # The manifests are embedded by the resource scripts, so the linker must not add its own.
    add_link_options(/MANIFEST:NO)
else()
    add_compile_options(-Wall -Wextra -ffunction-sections -fdata-sections)
    add_link_options(-static -Wl,--gc-sections $<$<NOT:$<CONFIG:Debug>>:-s>)
endif()

add_executable(evilvideo WIN32 src/gui/main.c src/gui/evilvideo.rc)
add_executable(evilvideo-cli src/cli/main.c src/cli/evilvideo-cli.rc)
