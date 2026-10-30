#!/bin/bash -e
# 生成 Android aarch64 cross file 并配置 Mesa 构建目录。
#
#   MESA_SRC=/path/to/mesa-26.3.0-rc1 \
#   NDK_DIR=/path/to/android-ndk-r30 \
#   [DEPS_DIR=/path/to/static-libdrm] \
#   [BUILD_DIR=./build/mesa-26.3] \
#   scripts/setup-build.sh
#
# DEPS_DIR 是可选的静态依赖前缀（lib/pkgconfig 下要有 libdrm.pc）。
set -o pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$script_dir/.." && pwd)"
[ -f "$repo/.env.local" ] && . "$repo/.env.local"

# 可选的本地工具链/bin（存在才用，例如仓库里自带的 meson/ninja）
TOOLS="$repo/vulkan-driver/.build-tools"
[ -d "$TOOLS/bin" ] && export PATH="$TOOLS/bin:$PATH"
[ -d "$TOOLS/lib/pydeps" ] && export PYTHONPATH="$TOOLS/lib/pydeps"

MESA_SRC="${MESA_SRC:?请设置 MESA_SRC 指向 Mesa 26.3 源码树}"
NDK_DIR="${NDK_DIR:?请设置 NDK_DIR 指向 Android NDK}"
BUILD_DIR="${BUILD_DIR:-$repo/build/mesa-26.3}"

[ -d "$MESA_SRC" ] || { echo "MESA_SRC 不存在: $MESA_SRC"; exit 1; }
[ -d "$NDK_DIR" ] || { echo "NDK_DIR 不存在: $NDK_DIR"; exit 1; }

mkdir -p "$BUILD_DIR"
cross="$BUILD_DIR/android-aarch64.ini"
sed -e "s|@NDK@|$NDK_DIR|g" -e "s|@DEPS@|${DEPS_DIR:-/nonexistent}|g" \
    "$script_dir/android-aarch64.ini.in" > "$cross"
if [ -z "${DEPS_DIR:-}" ]; then
    # 没有静态依赖前缀时，用系统 pkg-config
    sed -i "s|^pkg-config = .*|pkg-config = '/usr/bin/pkg-config'|" "$cross"
fi

meson setup "$BUILD_DIR" "$MESA_SRC" --cross-file "$cross" \
    -Dbuildtype=release -Dstrip=true \
    -Dplatforms=android -Dplatform-sdk-version=35 -Dandroid-stub=true \
    -Dgallium-drivers=freedreno -Dvulkan-drivers= -Dfreedreno-kmds=kgsl \
    -Degl=enabled -Dgles1=disabled -Dgles2=enabled -Dglx=disabled -Dgbm=disabled \
    -Dllvm=disabled -Dlmsensors=disabled -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=disabled \
    -Db_lto=false \
    -Dc_args="-march=armv8.2-a+dotprod+fp16" \
    -Dcpp_args="-march=armv8.2-a+dotprod+fp16"

echo
echo "构建目录: $BUILD_DIR"
echo "下一步: BUILD_DIR=$BUILD_DIR scripts/build-egl.sh"
