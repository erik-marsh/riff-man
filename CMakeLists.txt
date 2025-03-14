cmake_minimum_required(VERSION 3.16)
project(riff-man VERSION 0.1.0 LANGUAGES C CXX)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/raylib)

add_executable(riff-man
    main.cpp
    Renderer.cpp)
target_compile_features(riff-man PRIVATE cxx_std_23)
target_compile_options(riff-man PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wdisabled-optimization
    -Wno-missing-field-initializers
    -O2)
target_include_directories(riff-man PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/raylib/src/
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/clay/
)
target_link_libraries(riff-man PRIVATE raylib)
