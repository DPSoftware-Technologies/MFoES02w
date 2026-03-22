cmake_minimum_required(VERSION 3.20)

project(mfoes02w
    VERSION 1.0.0
    LANGUAGES CXX
)

#  Standards 
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#  Output directories 
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

#  Options 
option(STATIC_LINK    "Link statically (required for Alpine musl)" ON)
option(ENABLE_TESTS   "Build unit tests"                           OFF)
option(ENABLE_LOGGING "Enable debug logging"                       OFF)

#  Static linking for musl 
if(STATIC_LINK)
    # Link most things statically but NOT GPU/display libs
    add_link_options(
        -static-libgcc
        -static-libstdc++
        -Wl,--gc-sections
    )
    set(BUILD_SHARED_LIBS OFF)
    message(STATUS "Static linking ENABLED (musl-compatible)")
endif()

#  Compiler flags 
add_compile_options(
    -w
    -Wall
    -Wextra
    -Wpedantic
    -ffunction-sections
    -fdata-sections
)

# Strip dead code on release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_link_options(-Wl,--gc-sections -s)
endif()

if(ENABLE_LOGGING)
    add_compile_definitions(ENABLE_LOGGING)
endif()

# --------------library--------------------

add_subdirectory(libs/libgfx)
add_subdirectory(libs/hwinterface)
add_subdirectory(libs/uisys)


#  Subdirectories 
add_executable(mfoes02w
    src/main.cpp
    src/usb_handling.cpp
    src/ui.cpp
    src/app.cpp
)

target_include_directories(mfoes02w
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

# Link pthread statically if needed
find_package(Threads REQUIRED)
target_link_libraries(mfoes02w
    PRIVATE
        Threads::Threads
        libgfx
        pthread
        hwinterface 
        uisys 
)


#  Install rules 
install(TARGETS mfoes02w
    RUNTIME DESTINATION bin
)

#  Summary 
message(STATUS "")
message(STATUS "=== alpine-rpi-cmake build config ===")
message(STATUS "  Target arch   : ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  Compiler      : ${CMAKE_CXX_COMPILER}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Static link   : ${STATIC_LINK}")
message(STATUS "  Tests         : ${ENABLE_TESTS}")
message(STATUS "  Logging       : ${ENABLE_LOGGING}")
message(STATUS "======================================")
message(STATUS "")
