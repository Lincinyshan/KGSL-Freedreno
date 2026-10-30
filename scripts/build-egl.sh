#!/bin/bash -e
# 编译 Mesa 产物到 dist/。
#
# 构建目录需要先用 scripts/setup-build.sh 配置好（默认 ./build/mesa-26.3）。
# 也可以用 BUILD_DIR / OUT_DIR 覆盖，或写进仓库根的 .env.local（不入库）。
set -o pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$script_dir/.." && pwd)"
[ -f "$repo/.env.local" ] && . "$repo/.env.local"

BUILD_DIR="${BUILD_DIR:-$repo/build/mesa-26.3}"
OUT_DIR="${OUT_DIR:-$repo/dist}"
TOOLS="$repo/vulkan-driver/.build-tools"

# 可选的本地工具链/bin（存在才用）
[ -d "$TOOLS/bin" ] && export PATH="$TOOLS/bin:$PATH"
[ -d "$TOOLS/lib/pydeps" ] && export PYTHONPATH="$TOOLS/lib/pydeps"
[ -n "${CCACHE_DIR:-}" ] && export CCACHE_DIR
export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-6G}"

[ -d "$BUILD_DIR" ] || {
    echo "缺少构建目录: $BUILD_DIR"
    echo "先运行: MESA_SRC=... NDK_DIR=... $script_dir/setup-build.sh"
    exit 1
}

mkdir -p "$OUT_DIR"
ninja -C "$BUILD_DIR"

cp -f "$BUILD_DIR/src/egl/libEGL.so"                        "$OUT_DIR/libEGL_mesa_gl.so"
cp -f "$BUILD_DIR/src/gallium/targets/dri/libgallium_dri.so" "$OUT_DIR/libgallium_dri.so"

sha256sum "$OUT_DIR"/*.so
