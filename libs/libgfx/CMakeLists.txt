cmake_minimum_required(VERSION 3.10)
project(libgfx LANGUAGES CXX)

# ============================================================================
# Backend selection
#   Default  : Linux framebuffer /dev/fb0  (software render, no extra deps)
#   GFX_DRM_SUPPORT=ON : DRM/KMS dumb-buffer page-flip (raw kernel ioctls,
#              no Mesa / no libdrm runtime — just kernel UAPI headers)
#   GFX_SDL_SUPPORT=ON : SDL2 desktop preview window
#
# DRM and SDL are mutually exclusive; DRM takes priority if both are ON.
# ============================================================================
option(GFX_DRM_SUPPORT "Enable DRM/KMS backend (raw kernel ioctls, no Mesa)" OFF)
option(GFX_SDL_SUPPORT "Enable SDL2 desktop preview backend" ON)
option(GFX_NC5874_SUPPORT "Enable Nationalchip 5874 set-top-box OSD backend (bare metal, cross-compile)" OFF)

# ============================================================================
# Nationalchip 5874 backend — bare metal under U-Boot, no libc/libstdc++/FPU.
# Cross-compile with mipsel-linux-gnu-g++ (toolchain file) and link a
# soft-float runtime (compiler-rt builtins built -msoft-float): the toolchain's
# libgcc float helpers use FPU instructions the MIPS 24KEc does not have.
# The application provides gfx_nc5874_malloc/free/log/millis and
# memcpy/memmove/memset/memcmp/strlen/strcmp (see nc5874_std.h).
# ============================================================================
if(GFX_NC5874_SUPPORT)
    add_library(libgfx STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/GFX.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/DrawReplay.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nc5874_std.cpp
    )
    target_include_directories(libgfx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(libgfx PUBLIC cxx_std_14)
    target_compile_definitions(libgfx PUBLIC GFX_NC5874)
    target_compile_options(libgfx PUBLIC
        -march=mips32r2 -EL -msoft-float -ffreestanding -mno-abicalls -fno-pic -G 0
        -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
        -fno-asynchronous-unwind-tables)
    message(STATUS "GFX: backend = Nationalchip 5874 OSD (bare metal)")
    return()
endif()

# ============================================================================
# Windows / MinGW64: DRM and framebuffer are Linux-only; SDL2 is required.
# ============================================================================
if(WIN32 OR MINGW)
    if(GFX_DRM_SUPPORT)
        message(WARNING "GFX: DRM/KMS is Linux-only — disabling for Windows build")
        set(GFX_DRM_SUPPORT OFF CACHE BOOL "" FORCE)
    endif()
    if(NOT GFX_SDL_SUPPORT)
        message(FATAL_ERROR "GFX: Windows/MinGW64 builds require SDL2 (set GFX_SDL_SUPPORT=ON and install SDL2).")
    endif()
endif()

add_library(libgfx
    ${CMAKE_CURRENT_SOURCE_DIR}/GFX.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DrawReplay.cpp
)

target_include_directories(libgfx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(libgfx PUBLIC cxx_std_14)

# ============================================================================
# DRM/KMS backend — raw kernel ioctls, zero runtime library dependency.
# The inline UAPI definitions in GFX.cpp use _IOWR/_IOW from <sys/ioctl.h>.
# ============================================================================
if(GFX_DRM_SUPPORT)
    if(GFX_SDL_SUPPORT)
        message(STATUS "GFX: DRM_SUPPORT overrides SDL_SUPPORT")
        set(GFX_SDL_SUPPORT OFF)
    endif()
    target_compile_definitions(libgfx PUBLIC GFX_USE_DRM)
    message(STATUS "GFX: backend = DRM/KMS dumb-buffer (raw kernel ioctls, no Mesa)")
    return()  # skip SDL block below
endif()

# ============================================================================
# SDL2 desktop preview backend
# ============================================================================
if(GFX_SDL_SUPPORT)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        target_compile_definitions(libgfx PUBLIC GFXSDL)
        target_link_libraries(libgfx PUBLIC SDL2::SDL2)
        target_include_directories(libgfx PUBLIC ${SDL2_INCLUDE_DIRS})
        message(STATUS "GFX: backend = SDL2 desktop preview")
    else()
        if(WIN32 OR MINGW)
            message(FATAL_ERROR
                "GFX: SDL2 not found — required for Windows/MinGW64 builds.\n"
                "Install SDL2 development libraries (e.g. via MSYS2: "
                "pacman -S mingw-w64-x86_64-SDL2) and re-run CMake.")
        else()
            message(WARNING "GFX: SDL2 not found — falling back to framebuffer backend")
        endif()
    endif()
else()
    if(WIN32 OR MINGW)
        message(FATAL_ERROR
            "GFX: No usable backend for Windows/MinGW64 (GFX_SDL_SUPPORT is OFF "
            "and framebuffer/DRM are Linux-only). Enable GFX_SDL_SUPPORT and install SDL2.")
    else()
        message(STATUS "GFX: backend = Linux framebuffer /dev/fb0 (default)")
    endif()
endif()
