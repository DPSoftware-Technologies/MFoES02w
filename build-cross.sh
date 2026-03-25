#!/usr/bin/env sh
set -e

export PATH="/opt/aarch64-linux-musl-cross/bin:$PATH"

BUILD_DIR="build-aarch64"
BUILD_TYPE="${1:-Release}"
SYSROOT="/opt/alpine-aarch64-sysroot"

echo "Cross-compiling → aarch64 musl | type=${BUILD_TYPE}"

# build-cross.sh
cmake -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-alpine-musl.cmake \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DSTATIC_LINK=OFF \
    -DGFX_USE_FB=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

#  Bundle libs into build/libs 
# echo "Bundling shared libs..."
# mkdir -p "${BUILD_DIR}/libs"
# for lib in \
#     libEGL.so.1 libGLESv2.so.2 libgbm.so.1 libdrm.so.2 \
#     libwayland-client.so.0 libwayland-server.so.0 \
#     libxcb.so.1 libexpat.so.1 libglapi.so.0 libbsd.so.0; do
#     find "${SYSROOT}/usr/lib" "${SYSROOT}/lib" -name "${lib}*" \
#         -exec cp {} "${BUILD_DIR}/libs/" \; 2>/dev/null || true
# done

mkdir -p "${BUILD_DIR}/lib"
cp /opt/aarch64-linux-musl-cross/aarch64-linux-musl/lib/libstdc++.so.6 "${BUILD_DIR}/lib"
cp /opt/aarch64-linux-musl-cross/aarch64-linux-musl/lib/libgcc_s.so.1 "${BUILD_DIR}/lib"

ln -sf "${BUILD_DIR}/compile_commands.json" compile_commands.json
echo ""
echo "Binary : ${BUILD_DIR}/bin/mfoes02w"
echo "Libs   : ${BUILD_DIR}/lib/"