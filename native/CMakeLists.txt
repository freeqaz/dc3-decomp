cmake_minimum_required(VERSION 3.22)
project(dc3-native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(Dawn REQUIRED)

add_executable(dc3-native src/main.cpp)

target_link_libraries(dc3-native
    dawn::webgpu_dawn
)
