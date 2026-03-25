cmake_minimum_required(VERSION 3.16)
project(uisys)

add_library(uisys
    ${CMAKE_CURRENT_SOURCE_DIR}/button_widget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/slider_widget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dial_widget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/combobox_widget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/keyboard_widget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/textedit_widget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/spinbox_widget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dialog_widget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/manager.cpp
)

target_include_directories(uisys PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..   # include as "uisys/manager.h"
)

# uisys widgets need TouchEventData from gt911.h which lives in hwinterface
target_link_libraries(uisys 
    PUBLIC 
        hwinterface
        libgfx
)
