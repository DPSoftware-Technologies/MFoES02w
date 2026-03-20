cmake_minimum_required(VERSION 3.16)
project(uisys)

# Header-only interface library
add_library(uisys INTERFACE)

target_include_directories(uisys INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..   # include as "uisys/manager.h"
)

# uisys buttons need TouchEventData from gt911.h which lives in hwinterface
target_link_libraries(uisys INTERFACE hwinterface)