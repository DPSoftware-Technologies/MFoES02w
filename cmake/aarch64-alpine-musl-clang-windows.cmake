# cmake/aarch64-alpine-musl-clang-windows.cmake
#
# Cross-compile aarch64-linux-musl straight from a native Windows host using
# LLVM/clang (which is inherently multi-target — no gcc cross-binutils
# needed). Pairs with scripts/fetch-sysroot.cmd, which builds the sysroot
# this file points at.
set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(TARGET_TRIPLE aarch64-alpine-linux-musl)
set(ALPINE_SYSROOT "E:/sysroots/alpine-aarch64" CACHE PATH "Alpine aarch64 sysroot")

# Fall back to the default winget/installer location if clang isn't on PATH
# yet (PATH broadcast from the installer only reaches new shells).
set(LLVM_HINT_DIR "C:/Program Files/LLVM/bin")

find_program(CLANG_EXE   NAMES clang   HINTS "${LLVM_HINT_DIR}" REQUIRED)
find_program(CLANGXX_EXE NAMES clang++ HINTS "${LLVM_HINT_DIR}" REQUIRED)
find_program(LLVM_AR_EXE     NAMES llvm-ar     HINTS "${LLVM_HINT_DIR}")
find_program(LLVM_RANLIB_EXE NAMES llvm-ranlib HINTS "${LLVM_HINT_DIR}")
find_program(LLVM_STRIP_EXE  NAMES llvm-strip  HINTS "${LLVM_HINT_DIR}")

set(CMAKE_C_COMPILER   ${CLANG_EXE})
set(CMAKE_CXX_COMPILER ${CLANGXX_EXE})
set(CMAKE_C_COMPILER_TARGET   ${TARGET_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${TARGET_TRIPLE})
set(CMAKE_SYSROOT ${ALPINE_SYSROOT})

set(CMAKE_AR      ${LLVM_AR_EXE}     CACHE FILEPATH "")
set(CMAKE_RANLIB  ${LLVM_RANLIB_EXE} CACHE FILEPATH "")
set(CMAKE_STRIP   ${LLVM_STRIP_EXE}  CACHE FILEPATH "")

# lld ships with LLVM for Windows — avoids needing a cross gcc/binutils linker
add_link_options(-fuse-ld=lld)

# clang still needs GCC's crtbegin/crtend/libgcc runtime objects even though
# it isn't gcc itself — the "gcc" apk (fetched alongside musl/libstdc++)
# provides them under usr/lib/gcc/<triple>/<ver>/. Point clang at that dir
# via -B instead of hardcoding the version.
file(GLOB GCC_LIBDIR "${ALPINE_SYSROOT}/usr/lib/gcc/*/*")
if(GCC_LIBDIR)
    list(GET GCC_LIBDIR 0 GCC_LIBDIR)
    add_compile_options(-B${GCC_LIBDIR})
    add_link_options(-B${GCC_LIBDIR} -L${GCC_LIBDIR})
endif()

set(CMAKE_FIND_ROOT_PATH "${ALPINE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
