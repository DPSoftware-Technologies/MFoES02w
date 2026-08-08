cmake_minimum_required(VERSION 3.16)
project(hwinterface_with_southbridge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(hwinterface
    gpio_sysfs.cpp
    i2c_dev.cpp
    74hc165.cpp
    ads1115.cpp
    usbd_client.cpp
    gt911.cpp
    spi_dev.cpp
    southbridge.cpp
    northbridge.cpp
    pwm.cpp
)

target_include_directories(hwinterface PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    # nb_protocol.h lives with the firmware that implements it, and is shared
    # rather than copied so the two ends cannot drift apart.
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Firmware/northbridge/src
)

target_link_libraries(hwinterface PUBLIC pthread)