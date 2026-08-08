# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# Pull in FreeRTOS kernel (must be after pico_sdk_import.cmake so PICO_PLATFORM is known).
# Override with -DFREERTOS_KERNEL_PATH=... or the FREERTOS_KERNEL_PATH env var.
if (NOT FREERTOS_KERNEL_PATH AND NOT DEFINED ENV{FREERTOS_KERNEL_PATH})
    set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/../FreeRTOS-Kernel)
endif()
include(FreeRTOS_Kernel_import.cmake)

project(northbridge C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(northbridge
        src/main.cpp
        src/i2c_bus.cpp
        src/ds3231.cpp
        src/pcf8575.cpp
        src/dspi.cpp
        src/dspi_cmd.cpp
        src/build_time.cpp)

# Stamp the build time into a generated header on every build, so an
# incremental build never links a stale reference time.
set(BUILD_STAMP_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/build_stamp.h)
add_custom_target(gen_build_stamp
        BYPRODUCTS ${BUILD_STAMP_HEADER}
        COMMAND ${CMAKE_COMMAND} -DHEADER=${BUILD_STAMP_HEADER}
                -P ${CMAKE_CURRENT_LIST_DIR}/cmake/build_stamp.cmake
        COMMENT "Stamping build time")
add_dependencies(northbridge gen_build_stamp)

pico_set_program_name(northbridge "northbridge")
pico_set_program_version(northbridge "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(northbridge 0)
pico_enable_stdio_usb(northbridge 1)

# Add the standard library to the build
# FreeRTOS-Kernel-Heap4 = kernel + heap_4 allocator (swap for -Heap1/2/3/5 if needed)
target_link_libraries(northbridge
        pico_stdlib
        hardware_i2c
        hardware_spi
        hardware_dma
        FreeRTOS-Kernel-Heap4)

# Add the standard include files to the build
# src/ must be on the path: the kernel includes FreeRTOSConfig.h from there
target_include_directories(northbridge PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

pico_add_extra_outputs(northbridge)

