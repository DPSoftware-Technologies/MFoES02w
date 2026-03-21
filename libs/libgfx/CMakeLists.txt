cmake_minimum_required(VERSION 3.10)
project(libgfx LANGUAGES CXX)

option(GFX_USE_FB "Use legacy /dev/fb0 framebuffer backend instead of DRM dumb-buffer" OFF)

add_library(libgfx STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/GFX.cpp
)

target_include_directories(libgfx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}   # exposes GFX.h (pulls in drm/drm_mode.h via it)
)

# No extra link libraries needed:
#   DRM backend → raw ioctl() only, no -ldrm
#   FB  backend → raw mmap()/ioctl() only, no external libs
target_link_libraries(libgfx PUBLIC)

# GFX.h auto-selects GFX_USE_DRM_DUMB when neither flag is set, so we only
# need to inject GFX_USE_FB when the legacy backend is explicitly requested.
target_compile_definitions(libgfx PUBLIC
    $<$<BOOL:${GFX_USE_FB}>:GFX_USE_FB>
)

target_compile_features(libgfx PUBLIC cxx_std_14)
