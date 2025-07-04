cmake_minimum_required(VERSION 3.16)
project(riff-man VERSION 0.1.0 LANGUAGES C CXX)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/raylib)

# Install these system-wide. It will make your life easier.
# * harfbuzz
# * FreeType2
# * fribidi
set(RAQM_DEPENDENCY_INCLUDES
    /usr/include/fribidi/
    /usr/include/harfbuzz/
    /usr/include/freetype2/
    /usr/include/glib-2.0/
    /usr/lib64/glib-2.0/include/
)

set(RAQM_DEPENDENCY_LIBS
    fribidi
    harfbuzz
    freetype
)

add_executable(riff-man
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite/sqlite3.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/raqm/src/raqm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TextUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Layout.cpp
)
target_compile_features(riff-man PRIVATE cxx_std_23)
target_compile_options(riff-man PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wdisabled-optimization
    -Wno-missing-field-initializers
    -O2
    -g
    -Wshadow
    #-DRIFF_MAN_DEBUG_FONTS
    #-DRIFF_MAN_DEBUG_DEFER
)
target_include_directories(riff-man PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/raylib/src/
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/clay/
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite/
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/raqm/src/
    ${RAQM_DEPENDENCY_INCLUDES}
)
target_link_libraries(riff-man PRIVATE
    raylib
    ${RAQM_DEPENDENCY_LIBS}
)
