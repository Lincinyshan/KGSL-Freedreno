#!/bin/bash -e
# ===========================================================================
# Mesa 25.0.7 EGL + GLES + desktop-GL for freedreno/KGSL (FCL bridge build).
#
# 目的：给 FCL 的 gl_bridge（EGL 路径，POJAV_RENDERER=opengles3_desktopgl_*）
# 提供一套自编的 libEGL + libgallium_dri，ANativeWindow 直出（零拷贝），
# 不再走 OSMesa 的每帧回读。
#
# 前提：源码树已由 build-freedreno-osmesa.sh 打补丁（含 KGSL 后端）。
# ===========================================================================
set -o pipefail

green='\033[0;32m'; red='\033[0;31m'; yellow='\033[1;33m'; nocolor='\033[0m'

script_dir="$(cd "$(dirname "$0")" && pwd)"
workdir="$script_dir/gl_workdir"; [ -n "$WORKDIR" ] && workdir="$WORKDIR"
ndk="$workdir/android-ndk-r30"; [ -n "$NDK_DIR" ] && ndk="$NDK_DIR"
ndk_bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
tools_dir="$script_dir/../vulkan-driver/.build-tools"; [ -n "$TOOLS_DIR" ] && tools_dir="$TOOLS_DIR"

mesa_ver="$MESA_VER"; [ -z "$mesa_ver" ] && mesa_ver=25.0.7
sdkver="$DEVICE_SDK"; [ -z "$sdkver" ] && sdkver=35
jobs="$JOBS"; [ -z "$jobs" ] && jobs=2
mesadir="$workdir/mesa-$mesa_ver"
deps="$workdir/deps-static"
builddir="$workdir/build-egl-$mesa_ver"
outdir="$script_dir/release-$mesa_ver-a730-egl"

export PATH="$tools_dir/bin:$PATH"
export PYTHONPATH="$tools_dir/lib/pydeps"
export CCACHE_DIR="$script_dir/../vulkan-driver/Mesa-Turnip-Builder/.ccache"
export CCACHE_MAXSIZE="6G"
export TMPDIR="$workdir/tmp"
mkdir -p "$TMPDIR" "$CCACHE_DIR"

[ -d "$mesadir" ] || { echo -e "$red 缺少 $mesadir（先跑 build-freedreno-osmesa.sh） $nocolor"; exit 1; }
[ -f "$mesadir/src/freedreno/drm/kgsl/kgsl_pipe.c" ] || { echo -e "$red 源码未打 KGSL 补丁 $nocolor"; exit 1; }

cd "$mesadir"

# ---------------------------------------------------------------------------
# EGL 补丁（幂等）
# ---------------------------------------------------------------------------
python3 - <<'PYEOF'
import sys

# E1) 结构体加一个标记：KGSL 没有 DRM 节点，跳过依赖 DRM 的 device 初始化。
p = 'src/egl/drivers/dri2/egl_dri2.h'
s = open(p).read()
if 'kgsl_no_drm' not in s:
    anchor = '   bool pure_swrast;'
    if anchor not in s:
        sys.exit('egl_dri2.h: pure_swrast anchor not found')
    s = s.replace(anchor, anchor + '\n\n   /* FCL: /dev/kgsl-3d0 has no DRM node; skip DRM-only setup */\n   bool kgsl_no_drm;', 1)
    open(p, 'w').write(s)
    print('patched egl_dri2.h: kgsl_no_drm flag')

# E2) Android 平台初始化：没有 DRM 设备时直接开 /dev/kgsl-3d0，
#     用 freedreno DRI 驱动建屏幕（等价于 droid_probe_device 的硬件分支）。
p = 'src/egl/drivers/dri2/platform_android.c'
s = open(p).read()
if 'KGSL: no DRM node' not in s:
    anchor = '''   disp->DriverData = (void *)dri2_dpy;
   if (!force_pure_swrast)
      device_opened = droid_open_device(disp, disp->Options.ForceSoftware);
'''
    if anchor not in s:
        sys.exit('platform_android.c: droid_open_device anchor not found')
    insert = anchor + '''
   /* FCL: KGSL: no DRM node */
   if (!device_opened && !force_pure_swrast) {
      int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);

      if (kgsl_fd >= 0) {
         /* Our own libraries live next to libEGL; point the DRI loader at
          * them (the app's linker namespace does not include this dir). */
         Dl_info dl_info;
         if (dladdr((void *) dri2_initialize_android, &dl_info) && dl_info.dli_fname) {
            char dir[PATH_MAX];
            snprintf(dir, sizeof(dir), "%s", dl_info.dli_fname);
            char *slash = strrchr(dir, '/');
            if (slash) {
               *slash = '\\0';
               setenv("LIBGL_DRIVERS_PATH", dir, 0);
            }
         }

         dri2_dpy->fd_render_gpu = kgsl_fd;
         dri2_dpy->driver_name = strdup("freedreno");
         dri2_dpy->loader_extensions = droid_image_loader_extensions;
         if (dri2_load_driver(disp) && dri2_create_screen(disp)) {
            dri2_dpy->kgsl_no_drm = true;
            device_opened = EGL_TRUE;
         } else {
            _eglLog(_EGL_WARNING, "KGSL: failed to create freedreno screen");
            droid_unload_driver(disp);
            close(kgsl_fd);
            dri2_dpy->fd_render_gpu = -1;
         }
      }
   }
'''
    s = s.replace(anchor, insert, 1)
    old = '   if (!dri2_dpy->pure_swrast && !dri2_setup_device(disp, false)) {'
    new = '   if (!dri2_dpy->pure_swrast && !dri2_dpy->kgsl_no_drm &&\n       !dri2_setup_device(disp, false)) {'
    if old not in s:
        sys.exit('platform_android.c: dri2_setup_device anchor not found')
    s = s.replace(old, new, 1)
    open(p, 'w').write(s)
    print('patched platform_android.c: KGSL direct device open')

# E3) 本构建走系统 libsync（FCL 进程里 /system/lib64/libsync.so 已被系统 GL 加载）；
#     OSMesa 构建用的 ioctl fallback 会与 <sync/sync.h> 的声明冲突，这里恢复。
p = 'src/util/libsync.h'
s = open(p).read()
if '#if 0 /* FCL: use the ioctl fallback instead of libsync.so */' in s:
    s = s.replace('#if 0 /* FCL: use the ioctl fallback instead of libsync.so */',
                  '#if DETECT_OS_ANDROID', 1)
    open(p, 'w').write(s)
    print('patched libsync.h: restore system libsync for EGL build')
PYEOF

# ---------------------------------------------------------------------------
# EGL 相关补丁文件（整文件覆盖；目标路径见 patches/egl/MAP）
# 这些文件包含 KGSL 设备打开、Android 上开放桌面 GL、u_gralloc/pipe_loader
# 的 KGSL 回退、窗口尺寸兜底等改动，构建时直接替换源码树中的同名文件。
# ---------------------------------------------------------------------------
if [ -f "$script_dir/patches/egl/MAP" ]; then
    while read -r src dst; do
        [ -n "$src" ] || continue
        [ -f "$script_dir/patches/egl/$src" ] || continue
        cp "$script_dir/patches/egl/$src" "$mesadir/$dst"
    done < "$script_dir/patches/egl/MAP"
    echo "applied patches/egl/*"
fi

# ---------------------------------------------------------------------------
# cross file
# ---------------------------------------------------------------------------
cat > "$workdir/android-aarch64-egl.txt" <<EOF
[binaries]
ar = '$ndk_bin/llvm-ar'
c = ['ccache', '$ndk_bin/aarch64-linux-android$sdkver-clang', '-Wno-deprecated-declarations', '-Wno-gnu-alignof-expression']
cpp = ['ccache', '$ndk_bin/aarch64-linux-android$sdkver-clang++', '--start-no-unused-arguments', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-static-libstdc++', '--end-no-unused-arguments', '-Wno-error=c++11-narrowing', '-Wno-deprecated-declarations', '-Wno-gnu-alignof-expression']
c_ld = '$ndk_bin/ld.lld'
cpp_ld = '$ndk_bin/ld.lld'
strip = '$ndk_bin/llvm-strip'
pkg-config = ['env', 'PKG_CONFIG_LIBDIR=$deps/lib/pkgconfig', '/usr/bin/pkg-config']

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
EOF

for f in "$outdir" ; do mkdir -p "$f"; done

if [ ! -f "$builddir/build.ninja" ]; then
    rm -rf "$builddir"
    echo "Configuring Mesa $mesa_ver (EGL/GLES/GL + freedreno/KGSL)..."
    meson setup "$builddir" "$mesadir" --wrap-mode=nofallback \
        --cross-file "$workdir/android-aarch64-egl.txt" \
        -Dbuildtype=release -Dstrip=true \
        -Dplatforms=android -Dplatform-sdk-version="$sdkver" -Dandroid-stub=true \
        -Dgallium-drivers=freedreno -Dvulkan-drivers= -Dfreedreno-kmds=kgsl \
        -Degl=enabled -Dgles2=enabled -Dopengl=true \
        -Dglx=disabled -Dgbm=disabled -Dosmesa=false -Dllvm=disabled \
        -Dlmsensors=disabled -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=disabled \
        -Db_lto=false \
        -Dc_args=-march=armv8.2-a+dotprod+fp16 -Dcpp_args=-march=armv8.2-a+dotprod+fp16
fi

echo "Compiling (-j$jobs)..."
ninja -C "$builddir" -j "$jobs"

# ---------------------------------------------------------------------------
# 收集产物
# ---------------------------------------------------------------------------
egl_so="$builddir/src/egl/libEGL.so"
dri_so="$builddir/src/gallium/targets/dri/libgallium_dri.so"
gles2_so="$builddir/src/mesa/glapi/es2api/libGLESv2.so"
[ -f "$egl_so" ] && [ -f "$dri_so" ] || { echo -e "$red libEGL/libgallium_dri 未生成 $nocolor"; exit 1; }

cp "$egl_so" "$outdir/libEGL_freedreno.so"
cp "$dri_so" "$outdir/libgallium_dri.so"
[ -f "$gles2_so" ] && cp "$gles2_so" "$outdir/libGLESv2_freedreno.so"

for f in "$outdir/libEGL_freedreno.so" "$outdir/libgallium_dri.so" "$outdir/libGLESv2_freedreno.so"; do
    [ -f "$f" ] || continue
    "$ndk_bin/llvm-strip" --strip-unneeded "$f"
done
patchelf --set-soname libEGL_freedreno.so "$outdir/libEGL_freedreno.so"
patchelf --set-soname libGLESv2_freedreno.so "$outdir/libGLESv2_freedreno.so" 2>/dev/null || true

echo "== 产物 =="
ls -la "$outdir"
for f in "$outdir"/*.so; do
    echo "--- $f"
    "$ndk_bin/llvm-readelf" -d "$f" | grep -E "NEEDED|SONAME" | sed "s/^/    /"
done