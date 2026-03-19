cmake_minimum_required(VERSION 3.10)
project(libgfx LANGUAGES CXX)

# ── Backend selection ─────────────────────────────────────────────────────────
# Default: framebuffer (/dev/fb0)
# Set -DGFX_USE_OPENGL_ES=ON to use OpenGL ES 2.0 via EGL/DRM/GBM
option(GFX_USE_OPENGL_ES "Use OpenGL ES 2.0 + EGL + GBM + DRM backend instead of /dev/fb0" OFF)

# ── OpenGL ES dependencies (only pulled in when backend is selected) ───────────
if(GFX_USE_OPENGL_ES)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DRM  REQUIRED libdrm)
    pkg_check_modules(EGL  REQUIRED egl)
    pkg_check_modules(GLES REQUIRED glesv2)
    pkg_check_modules(GBM  REQUIRED gbm)
endif()

# ── Static library ────────────────────────────────────────────────────────────
add_library(libgfx STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/GFX.cpp
)

target_include_directories(libgfx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}                                          # exposes GFX.h
    $<$<BOOL:${GFX_USE_OPENGL_ES}>:${DRM_INCLUDE_DIRS}>
    $<$<BOOL:${GFX_USE_OPENGL_ES}>:${EGL_INCLUDE_DIRS}>
    $<$<BOOL:${GFX_USE_OPENGL_ES}>:${GLES_INCLUDE_DIRS}>
    $<$<BOOL:${GFX_USE_OPENGL_ES}>:${GBM_INCLUDE_DIRS}>
)

target_link_libraries(libgfx PUBLIC
    $<$<BOOL:${GFX_USE_OPENGL_ES}>:${EGL_LIBRARIES}>
    $<$<BOOL:${GFX_USE_OPENGL_ES}>:${GLES_LIBRARIES}>
    $<$<BOOL:${GFX_USE_OPENGL_ES}>:${GBM_LIBRARIES}>
    $<$<BOOL:${GFX_USE_OPENGL_ES}>:${DRM_LIBRARIES}>
)

# ── Propagate the backend define to GFX.h consumers ──────────────────────────
# This is what flips the #ifdef GFX_USE_OPENGL_ES branch in the header
target_compile_definitions(libgfx PUBLIC
    $<$<BOOL:${GFX_USE_OPENGL_ES}>:GFX_USE_OPENGL_ES>
)

target_compile_features(libgfx PUBLIC cxx_std_14)