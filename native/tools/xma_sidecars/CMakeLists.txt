# dc3-xma-sidecars -- derives the web build's kXMA SFX sidecars from every
# .milo_xbox (see dc3_xma_sidecars.cpp). Standalone on purpose: it needs only
# libav (DC3's own XmaSampleDecoder.cpp, the decoder the desktop build uses)
# and zlib, not the engine, Dawn or the decomp sources. Built and run by
# scripts/web/gen_sfx_sidecars.sh.
cmake_minimum_required(VERSION 3.16)
project(dc3-xma-sidecars CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil)
find_package(ZLIB REQUIRED)

set(DC3_NATIVE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
add_executable(dc3-xma-sidecars
    dc3_xma_sidecars.cpp
    ${DC3_NATIVE_SRC}/platform/XmaSampleDecoder.cpp)
target_include_directories(dc3-xma-sidecars PRIVATE ${DC3_NATIVE_SRC})
target_compile_definitions(dc3-xma-sidecars PRIVATE HX_FFMPEG=1)
target_link_libraries(dc3-xma-sidecars PRIVATE PkgConfig::FFMPEG ZLIB::ZLIB)
