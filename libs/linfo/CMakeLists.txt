cmake_minimum_required(VERSION 3.16)
project(linfo)

add_library(linfo
    ${CMAKE_CURRENT_SOURCE_DIR}/linfo.cpp
)

target_include_directories(linfo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
