cmake_minimum_required(VERSION 3.10)
project(libgfx LANGUAGES CXX)

# ============================================================================
# SDL2 Support (optional)
# ============================================================================
option(GFX_SDL_SUPPORT "Enable SDL2 desktop backend support" ON)

if(GFX_SDL_SUPPORT)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        message(STATUS "GFX: SDL2 support enabled")
    else()
        message(WARNING "GFX: SDL2 not found. SDL support disabled. Install SDL2-devel to enable.")
        set(GFX_SDL_SUPPORT OFF)
    endif()
else()
    message(STATUS "GFX: SDL2 support disabled (embedded/framebuffer backend)")
endif()

add_library(libgfx
    ${CMAKE_CURRENT_SOURCE_DIR}/GFX.cpp
)

target_include_directories(libgfx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}   # exposes GFX.h (pulls in drm/drm_mode.h via it)
)

target_compile_features(libgfx PUBLIC cxx_std_14)

# ============================================================================
# SDL2 Configuration
# ============================================================================
if(GFX_SDL_SUPPORT AND SDL2_FOUND)
    target_compile_definitions(libgfx PUBLIC GFXSDL)
    target_link_libraries(libgfx PUBLIC SDL2::SDL2)
    target_include_directories(libgfx PUBLIC ${SDL2_INCLUDE_DIRS})
    message(STATUS "GFX: Compiled with SDL2 support")
endif()
