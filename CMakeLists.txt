cmake_minimum_required(VERSION 3.10)
project(bacnet-edge-clientapp C)

set(CMAKE_C_STANDARD 99)

file(GLOB SOURCES "src/*.c")
add_executable(bacnet-edge-clientapp ${SOURCES})

target_include_directories(bacnet-edge-clientapp PRIVATE include)

# Conan/integrate dependencies
find_package(bacnet-stack REQUIRED CONFIG)
target_link_libraries(bacnet-edge-clientapp PRIVATE bacnet-stack::bacnet-stack)
