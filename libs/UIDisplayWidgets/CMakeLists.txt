cmake_minimum_required(VERSION 3.16)
project(UIDisplayWidgets)

add_library(UIDisplayWidgets
    ${CMAKE_CURRENT_SOURCE_DIR}/UIwidget.cpp
)

target_include_directories(UIDisplayWidgets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(UIDisplayWidgets 
    PUBLIC 
        libgfx
)
