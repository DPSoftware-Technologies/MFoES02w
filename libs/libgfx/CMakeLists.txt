cmake_minimum_required(VERSION 3.10)
project(libgfx LANGUAGES CXX)


add_library(libgfx
    ${CMAKE_CURRENT_SOURCE_DIR}/GFX.cpp
)

target_include_directories(libgfx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}   # exposes GFX.h (pulls in drm/drm_mode.h via it)
)

target_compile_features(libgfx PUBLIC cxx_std_14)
