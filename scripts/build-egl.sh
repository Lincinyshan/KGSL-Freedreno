#!/bin/bash -e
# 构建 Mesa 26.3 的 EGL + gallium(freedreno, KGSL 后端) 产物到 dist/
#
#   源码树 : vulkan-driver/Mesa-Turnip-Builder/turnip_workdir/mesa-mesa-26.3.0-rc1
#   构建目录: exp-opengl-driver/gl_workdir/build-egl-26.3  ← 路径被 meson 写死，别改名
#   产物    : dist/libEGL_mesa_gl.so, dist/libgallium_dri.so
#
# 构建目录由 meson 预先配置（cross file 在 gl_workdir/android-aarch64-egl.txt），
# 这里只负责 ninja + 拷贝。
set -o pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$script_dir/.." && pwd)"
workdir="$repo/exp-opengl-driver/gl_workdir"
builddir="${BUILD_DIR:-$workdir/build-egl-26.3}"
outdir="${OUT_DIR:-$repo/dist}"
tools="$repo/vulkan-driver/.build-tools"

export PATH="$tools/bin:$PATH"
export PYTHONPATH="$tools/lib/pydeps"
export CCACHE_DIR="$repo/vulkan-driver/Mesa-Turnip-Builder/.ccache"
export CCACHE_MAXSIZE=6G

[ -d "$builddir" ] || { echo "缺少构建目录 $builddir（meson 需要先 setup）"; exit 1; }
mkdir -p "$outdir"

ninja -C "$builddir"

cp -f "$builddir/src/egl/libEGL.so"                        "$outdir/libEGL_mesa_gl.so"
cp -f "$builddir/src/gallium/targets/dri/libgallium_dri.so" "$outdir/libgallium_dri.so"

sha256sum "$outdir"/*.so
