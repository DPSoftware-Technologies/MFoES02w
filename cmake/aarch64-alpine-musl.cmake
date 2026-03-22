# cmake/aarch64-alpine-musl.cmake
set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CROSS_TRIPLE "aarch64-linux-musl")
find_program(CMAKE_C_COMPILER   ${CROSS_TRIPLE}-gcc    REQUIRED)
find_program(CMAKE_CXX_COMPILER ${CROSS_TRIPLE}-g++    REQUIRED)
find_program(CMAKE_AR           ${CROSS_TRIPLE}-ar     )
find_program(CMAKE_RANLIB       ${CROSS_TRIPLE}-ranlib )
find_program(CMAKE_STRIP        ${CROSS_TRIPLE}-strip  )
set(CMAKE_EXE_LINKER_FLAGS 
    "${CMAKE_EXE_LINKER_FLAGS} -L/opt/alpine-aarch64-sysroot/usr/lib -Wl,-rpath-link,/opt/alpine-aarch64-sysroot/usr/lib -Wl,-rpath-link,/opt/alpine-aarch64-sysroot/lib -lpthread")
    
#  Alpine aarch64 sysroot 
set(ALPINE_SYSROOT "/opt/alpine-aarch64-sysroot")
set(CMAKE_FIND_ROOT_PATH "${ALPINE_SYSROOT}")

#  pkg-config pointing at Alpine sysroot 
set(ENV{PKG_CONFIG_PATH}        "${ALPINE_SYSROOT}/usr/lib/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR}      "${ALPINE_SYSROOT}/usr/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${ALPINE_SYSROOT}")

#  Search paths 
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)