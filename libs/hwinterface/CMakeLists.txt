cmake_minimum_required(VERSION 3.16)
project(hwinterface)

add_library(hwinterface
    gpio_sysfs.cpp
    i2c_dev.cpp
    74hc165.cpp
    ads1115.cpp
    usbd_client.cpp
    gt911.cpp
)

target_include_directories(hwinterface PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(hwinterface PUBLIC pthread)
