cmake_minimum_required(VERSION 3.16)
project(hwinterface)

# Header-only — all drivers are .h only, no .cpp
add_library(hwinterface INTERFACE)

target_include_directories(hwinterface INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# pthread required by gt911.h (background poll thread)
target_link_libraries(hwinterface INTERFACE pthread)