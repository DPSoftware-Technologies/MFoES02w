#!/usr/bin/env sh
set -e

export PATH="/opt/aarch64-linux-musl-cross/bin:$PATH"

BUILD_DIR="build-aarch64"
BUILD_TYPE="${1:-Release}"
SYSROOT="/opt/alpine-aarch64-sysroot"

echo "Cross-compiling → aarch64 musl | type=${BUILD_TYPE}"

# build-cross.sh
# GFX backend: DRM=ON uses kernel DRM/KMS dumb-buffer page-flip (no Mesa).
# Set GFX_DRM_SUPPORT=OFF to fall back to /dev/fb0 framebuffer.
GFX_DRM="${GFX_DRM_SUPPORT:-OFF}"

cmake -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-alpine-musl.cmake \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DSTATIC_LINK=OFF \
    -DGFX_DRM_SUPPORT="${GFX_DRM}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

mkdir -p "${BUILD_DIR}/lib"
cp /opt/aarch64-linux-musl-cross/aarch64-linux-musl/lib/libstdc++.so.6 "${BUILD_DIR}/lib"
cp /opt/aarch64-linux-musl-cross/aarch64-linux-musl/lib/libgcc_s.so.1 "${BUILD_DIR}/lib"

ln -sf "${BUILD_DIR}/compile_commands.json" compile_commands.json
echo ""
echo "Binary : ${BUILD_DIR}/bin/mfoes02w"
echo "Libs   : ${BUILD_DIR}/lib/"