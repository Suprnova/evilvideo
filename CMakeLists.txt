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
    # Standard C functions such as sscanf stay in use; the _s replacements MSVC suggests are not portable to MinGW.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # The manifests are embedded by the resource scripts, so the linker must not add its own.
    add_link_options(/MANIFEST:NO)
else()
    add_compile_options(-Wall -Wextra -ffunction-sections -fdata-sections)
    add_link_options(-static -Wl,--gc-sections $<$<NOT:$<CONFIG:Debug>>:-s>)
endif()

add_library(evcore STATIC src/core/games.c src/core/proc.c src/core/ffmpeg.c src/core/files.c src/core/rad.c)
target_include_directories(evcore PUBLIC src/core)

add_executable(evilvideo WIN32 src/gui/main.c src/gui/evilvideo.rc)
add_executable(evilvideo-cli src/cli/main.c src/cli/evilvideo-cli.rc)
target_link_libraries(evilvideo PRIVATE evcore)
target_link_libraries(evilvideo-cli PRIVATE evcore)

enable_testing()
add_executable(evtests tests/main.c tests/games_test.c tests/proc_test.c tests/ffmpeg_test.c tests/files_test.c tests/rad_test.c)
target_link_libraries(evtests PRIVATE evcore shell32)
add_test(NAME unit COMMAND evtests)
set_tests_properties(unit PROPERTIES TIMEOUT 60)
