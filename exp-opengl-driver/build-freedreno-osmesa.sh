#!/bin/bash -e
# ===========================================================================
# Mesa freedreno OSMesa build for Android aarch64 / Adreno 730 (SM8475)
# ---------------------------------------------------------------------------
# Target: FCL (FoldCraftLauncher) renderer plugin, "gallium_freedreno" path.
#   * Mesa 25.0.7 is the last upstream release that still ships OSMesa
#     (removed in 25.1.0).  freedreno reports GLSL 4.60 for a6xx+ since 24.x,
#     so this build exposes OpenGL 4.6 core.
#   * OSMesa is patched to create a real freedreno screen through KGSL
#     (/dev/kgsl-3d0) when GALLIUM_DRIVER=freedreno, which is exactly what
#     FCL's egl_bridge.c sets for POJAV_RENDERER=gallium_freedreno.
#   * libdrm is linked statically (no libdrm.so dependency at runtime).
# ===========================================================================
set -o pipefail

green='\033[0;32m'; red='\033[0;31m'; yellow='\033[1;33m'; nocolor='\033[0m'

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
workdir="${WORKDIR:-$script_dir/gl_workdir}"
ndk="${NDK_DIR:-$workdir/android-ndk-r30}"
ndk_bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
tools_dir="${TOOLS_DIR:-$script_dir/../vulkan-driver/.build-tools}"

mesa_ver="${MESA_VER:-25.0.7}"
sdkver="${DEVICE_SDK:-35}"
jobs="${JOBS:-2}"
mesadir="$workdir/mesa-$mesa_ver"
deps="$workdir/deps-static"
builddir="$workdir/build-osmesa-$mesa_ver"
outdir="$script_dir/release-$mesa_ver-a730"

export PATH="$tools_dir/bin:$PATH"
export PYTHONPATH="$tools_dir/lib/pydeps${PYTHONPATH:+:$PYTHONPATH}"
export CCACHE_DIR="${CCACHE_DIR:-$script_dir/../vulkan-driver/Mesa-Turnip-Builder/.ccache}"
export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-6G}"
export TMPDIR="$workdir/tmp"
mkdir -p "$TMPDIR" "$CCACHE_DIR"

[ -x "$ndk_bin/aarch64-linux-android$sdkver-clang" ] || { echo -e "$red NDK r30 not found at $ndk $nocolor"; exit 1; }
command -v patchelf >/dev/null || { echo -e "$red patchelf missing $nocolor"; exit 1; }

# ---------------------------------------------------------------------------
# Static libdrm (only .a, so the final .so has no libdrm.so dependency)
# ---------------------------------------------------------------------------
if [ ! -f "$deps/lib/pkgconfig/libdrm.pc" ]; then
    echo "Building static libdrm..."
    libdrm_src="$(ls -d "$workdir"/libdrm-2.4.* 2>/dev/null | head -n1)"
    [ -n "$libdrm_src" ] || { echo -e "$red libdrm source missing in $workdir $nocolor"; exit 1; }
    cat > "$workdir/android-aarch64.txt" <<EOF
[binaries]
ar = '$ndk_bin/llvm-ar'
c = ['$ndk_bin/aarch64-linux-android$sdkver-clang', '-Wno-deprecated-declarations']
cpp = ['$ndk_bin/aarch64-linux-android$sdkver-clang++', '-Wno-deprecated-declarations']
strip = '$ndk_bin/llvm-strip'
pkg-config = ['/usr/bin/pkg-config']

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
EOF
    ( cd "$libdrm_src"
      rm -rf build-static
      meson setup build-static --cross-file "$workdir/android-aarch64.txt" \
          --prefix="$deps" -Ddefault_library=static -Dbuildtype=release \
          -Dtests=false -Dman-pages=disabled -Dudev=false -Dvalgrind=disabled \
          -Dintel=disabled -Dradeon=disabled -Damdgpu=disabled -Dnouveau=disabled \
          -Dvmwgfx=disabled -Dfreedreno=disabled -Dvc4=disabled -Detnaviv=disabled \
          -Dinstall-test-programs=false >/dev/null
      ninja -C build-static -j"$jobs" install >/dev/null
    )
fi

# ---------------------------------------------------------------------------
# Mesa source
# ---------------------------------------------------------------------------
if [ ! -f "$mesadir/meson.build" ]; then
    tarball="$workdir/mesa-$mesa_ver.tar.xz"
    if [ ! -s "$tarball" ] || ! xz -t "$tarball" 2>/dev/null; then
        echo "Downloading Mesa $mesa_ver..."
        curl -fL --retry 3 --retry-delay 3 -C - -m 3600 \
            -o "$tarball" "https://archive.mesa3d.org/mesa-$mesa_ver.tar.xz"
    fi
    echo "Extracting Mesa $mesa_ver..."
    tar -xJf "$tarball" -C "$workdir"
fi

cd "$mesadir"

# ---------------------------------------------------------------------------
# Patches (idempotent)
# ---------------------------------------------------------------------------
python3 - <<'PYEOF'
import sys

# 1) Report the Adreno 730 v3 chip-id (0x07030002) as "Adreno (TM) 730" instead
#    of "FD725"; only the name string changes, chip data stays untouched.
p = 'src/freedreno/common/freedreno_devices.py'
s = open(p).read()
if 'name="FD725"' in s:
    s = s.replace('name="FD725"', 'name="Adreno (TM) 730"')
    open(p, 'w').write(s)
    print('patched GPU name: FD725 -> Adreno (TM) 730')

# 2) Let OSMesa create a freedreno/KGSL screen when GALLIUM_DRIVER=freedreno.
p = 'src/gallium/targets/osmesa/osmesa_target.c'
s = open(p).read()
if 'osmesa_create_freedreno_screen' not in s:
    s = s.replace('#include "sw/null/null_sw_winsys.h"',
'''#include "sw/null/null_sw_winsys.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* freedreno screen creation (Gallium driver, KGSL backend on Android). */
struct pipe_screen_config;
struct renderonly;
struct pipe_screen *fd_screen_create(int fd,
                                     const struct pipe_screen_config *config,
                                     struct renderonly *ro);

static struct pipe_screen *
osmesa_create_freedreno_screen(void)
{
   int fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
   if (fd < 0)
      return NULL;
   return fd_screen_create(fd, NULL, NULL);
}''', 1)

    old = '''struct pipe_screen *
osmesa_create_screen(void)
{
   struct sw_winsys *winsys;
   struct pipe_screen *screen;
'''
    new = old + '''
   /* FCL/Amethyst: POJAV_RENDERER=gallium_freedreno sets this. */
   if (getenv("GALLIUM_DRIVER") &&
       !strcmp(getenv("GALLIUM_DRIVER"), "freedreno")) {
      screen = osmesa_create_freedreno_screen();
      if (!screen)
         return NULL;
      return debug_screen_wrap(screen);
   }
'''
    if old not in s:
        sys.exit('osmesa_create_screen() body not found, patch failed')
    s = s.replace(old, new, 1)
    open(p, 'w').write(s)
    print('patched osmesa_target.c: freedreno/KGSL screen')

# 3) Link the freedreno driver (and static libdrm) into libOSMesa.
p = 'src/gallium/targets/osmesa/meson.build'
s = open(p).read()
if 'driver_freedreno' not in s:
    old = 'dep_ws2_32, dep_thread, dep_clock, dep_unwind, driver_swrast, idep_mesautil,'
    new = 'dep_ws2_32, dep_thread, dep_clock, dep_unwind, dep_libdrm, driver_swrast, driver_freedreno, idep_mesautil,'
    if old not in s:
        sys.exit('osmesa meson dependencies block not found, patch failed')
    s = s.replace(old, new, 1)
    open(p, 'w').write(s)
    print('patched osmesa meson: driver_freedreno + dep_libdrm')

# 4) Avoid runtime dependencies on non-public Android system libraries:
#      libcutils.so (atrace + property_get) and libsync.so (sync_wait/merge).
p = 'src/util/perf/cpu_trace.h'
s = open(p).read()
if '#elif DETECT_OS_ANDROID && !defined(__cplusplus)' in s:
    s = s.replace('#elif DETECT_OS_ANDROID && !defined(__cplusplus)',
                  '#elif 0 /* FCL: atrace disabled, avoids libcutils dependency */', 1)
    open(p, 'w').write(s)
    print('patched cpu_trace.h: atrace disabled')

p = 'src/util/os_misc.c'
s = open(p).read()
changed = False
if '#  include <cutils/properties.h>' in s:
    s = s.replace('#  include <cutils/properties.h>',
                  '#  include <sys/system_properties.h>', 1)
    changed = True
if 'PROPERTY_VALUE_MAX PROP_VALUE_MAX' not in s:
    s = s.replace('#  include <sys/system_properties.h>',
                  '#  include <sys/system_properties.h>\n'
                  '#  ifndef PROPERTY_VALUE_MAX\n'
                  '#  define PROPERTY_VALUE_MAX PROP_VALUE_MAX\n'
                  '#  endif\n'
                  '#  ifndef PROPERTY_KEY_MAX\n'
                  '#  define PROPERTY_KEY_MAX PROP_NAME_MAX\n'
                  '#  endif', 1)
    changed = True
if 'property_get(full_key' in s:
    s = s.replace('len = property_get(full_key, os_android_option_value, NULL);',
                  'len = __system_property_get(full_key, os_android_option_value);', 1)
    changed = True
if changed:
    open(p, 'w').write(s)
    print('patched os_misc.c: system_properties, no libcutils')

p = 'src/util/libsync.h'
s = open(p).read()
if '#if DETECT_OS_ANDROID' in s:
    s = s.replace('#if DETECT_OS_ANDROID',
                  '#if 0 /* FCL: use the ioctl fallback instead of libsync.so */', 1)
    open(p, 'w').write(s)
    print('patched libsync.h: ioctl fallback on Android')

# 5) KGSL Gallium backend (backported from SolDev69/freedreno-gallium-mesa,
#    branch feat/mesa-update/24.1, adapted to 25.0.7).
p = 'src/freedreno/drm/meson.build'
s = open(p).read()
if 'kgsl/kgsl_device.c' not in s:
    anchor = "libfreedreno_drm_files += libfreedreno_drm_msm_files\n"
    extra = """
libfreedreno_kgsl_files = files(
  'kgsl/kgsl_bo.c',
  'kgsl/kgsl_device.c',
  'kgsl/kgsl_pipe.c',
  'kgsl/kgsl_ringbuffer_sp.c',
  'kgsl/kgsl_priv.h',
)
if freedreno_kmds.contains('kgsl')
  libfreedreno_drm_files += libfreedreno_kgsl_files
  libfreedreno_drm_flags += '-DHAVE_FREEDRENO_KGSL=1'
endif
"""
    if anchor not in s:
        sys.exit('freedreno drm meson anchor not found')
    s = s.replace(anchor, anchor + extra, 1)
    open(p, 'w').write(s)
    print('patched freedreno drm meson: kgsl backend')

p = 'src/freedreno/drm/freedreno_device.c'
s = open(p).read()
if 'kgsl_device_new(int fd);' not in s:
    s = s.replace('struct fd_device *msm_device_new(int fd, drmVersionPtr version);',
                  'struct fd_device *msm_device_new(int fd, drmVersionPtr version);\n'
                  '#if HAVE_FREEDRENO_KGSL\n'
                  'struct fd_device *kgsl_device_new(int fd);\n'
                  '#endif', 1)
    open(p, 'w').write(s)
    print('patched freedreno_device.c: kgsl_device_new declaration')

# 6) KGSL: Android's /dev/kgsl-3d0 does not implement DRM_IOCTL_VERSION, so
#    drmGetVersion() fails with ENOTTY even for a valid kgsl fd.  Probe KGSL
#    directly instead (same approach as turnip's tu_enumerate_devices()).
p = 'src/freedreno/drm/freedreno_device.c'
s = open(p).read()
if 'ENOTTY' not in s:
    old = '''   version = drmGetVersion(fd);
   if (!version) {
      ERROR_MSG("cannot get version: %s", strerror(errno));
      return NULL;
   }'''
    new = '''   version = drmGetVersion(fd);
   if (!version) {
#if HAVE_FREEDRENO_KGSL
      /* Android's KGSL does not implement DRM_IOCTL_VERSION (ENOTTY), so
       * drmGetVersion() fails even for a perfectly valid /dev/kgsl-3d0 fd.
       * Probe it directly, the same way turnip's tu_enumerate_devices() does.
       */
      dev = kgsl_device_new(fd);
      if (dev)
         goto out;
#endif
      ERROR_MSG("cannot get version: %s", strerror(errno));
      return NULL;
   }'''
    if old not in s:
        sys.exit('fd_device_new() version detection block not found, patch failed')
    s = s.replace(old, new, 1)
    open(p, 'w').write(s)
    print('patched freedreno_device.c: kgsl probe when drmGetVersion fails')

# 7) OSMesa creates the freedreno screen without a loader-provided
#    pipe_screen_config (config == NULL); freedreno_screen.c assumes it is
#    non-NULL and would crash on config->options.
p = 'src/gallium/drivers/freedreno/freedreno_screen.c'
s = open(p).read()
if 'no loader-provided driconf' not in s:
    old = '''   /* parse driconf configuration now for device specific overrides: */
   driParseConfigFiles(config->options, config->options_info, 0, "msm",
                       NULL, fd_dev_name(screen->dev_id), NULL, 0, NULL, 0);

   screen->driconf.conservative_lrz =
         !driQueryOptionb(config->options, "disable_conservative_lrz");
   screen->driconf.enable_throttling =
         !driQueryOptionb(config->options, "disable_throttling");
   screen->driconf.dual_color_blend_by_location =
         driQueryOptionb(config->options, "dual_color_blend_by_location");
'''
    new = '''   /* parse driconf configuration now for device specific overrides: */
   if (config) {
      driParseConfigFiles(config->options, config->options_info, 0, "msm",
                          NULL, fd_dev_name(screen->dev_id), NULL, 0, NULL, 0);

      screen->driconf.conservative_lrz =
            !driQueryOptionb(config->options, "disable_conservative_lrz");
      screen->driconf.enable_throttling =
            !driQueryOptionb(config->options, "disable_throttling");
      screen->driconf.dual_color_blend_by_location =
            driQueryOptionb(config->options, "dual_color_blend_by_location");
   } else {
      /* no loader-provided driconf (standalone OSMesa target): defaults */
      screen->driconf.conservative_lrz = true;
      screen->driconf.enable_throttling = true;
      screen->driconf.dual_color_blend_by_location = false;
   }
'''
    if old not in s:
        sys.exit('freedreno_screen.c driconf block not found, patch failed')
    s = s.replace(old, new, 1)
    open(p, 'w').write(s)
    print('patched freedreno_screen.c: tolerate NULL pipe_screen_config')

# 8) Report KGSL screen-creation failures on stderr (FCL captures it) and stop
#    leaking the /dev/kgsl-3d0 fd.
p = 'src/gallium/targets/osmesa/osmesa_target.c'
s = open(p).read()
if 'osmesa/freedreno: open(' not in s:
    s = s.replace('#include <fcntl.h>',
                  '#include <errno.h>\n#include <fcntl.h>\n#include <stdio.h>', 1)
    old = '''static struct pipe_screen *
osmesa_create_freedreno_screen(void)
{
   int fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
   if (fd < 0)
      return NULL;
   return fd_screen_create(fd, NULL, NULL);
}'''
    new = '''static struct pipe_screen *
osmesa_create_freedreno_screen(void)
{
   struct pipe_screen *screen;
   int fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);

   if (fd < 0) {
      fprintf(stderr, "osmesa/freedreno: open(/dev/kgsl-3d0) failed: %s\\n",
              strerror(errno));
      return NULL;
   }

   screen = fd_screen_create(fd, NULL, NULL);
   if (!screen) {
      fprintf(stderr, "osmesa/freedreno: fd_screen_create() failed "
                      "(see MESA logs)\\n");
      close(fd);
      return NULL;
   }

   /* fd_screen_create() dups() the fd internally, ours is not needed. */
   close(fd);
   return screen;
}'''
    if old not in s:
        sys.exit('osmesa_create_freedreno_screen() body not found, patch failed')
    s = s.replace(old, new, 1)
    open(p, 'w').write(s)
    print('patched osmesa_target.c: kgsl error reporting')

# 9) ir3 shader disk cache: build_id_find_nhdr_for_addr() returns NULL when the
#    binary has no .note.gnu.build-id; build_id_data(NULL) is NULL+16 and
#    _mesa_sha1_format() then dereferences it.  Release builds have NDEBUG
#    asserts compiled out, so crash instead of assert - skip the cache instead.
p = 'src/freedreno/ir3/ir3_disk_cache.c'
s = open(p).read()
if 'No usable build-id note' not in s:
    old = '''   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(ir3_disk_cache_init);
   assert(note && build_id_length(note) == 20); /* sha1 */
'''
    new = '''   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(ir3_disk_cache_init);
   if (!note || build_id_length(note) != 20) {
      /* No usable build-id note: skip the cache rather than crash. */
      return;
   }
   assert(note && build_id_length(note) == 20); /* sha1 */
'''
    if old not in s:
        sys.exit('ir3_disk_cache_init() build-id block not found, patch failed')
    s = s.replace(old, new, 1)
    open(p, 'w').write(s)
    print('patched ir3_disk_cache.c: tolerate missing build-id note')

# 10) Emit a GNU build-id in libOSMesa: Mesa expects one (ir3 disk cache, crash
#     symbolization).  ld.lld only adds it when asked.
p = 'src/gallium/targets/osmesa/meson.build'
s = open(p).read()
if '--build-id' not in s:
    old = 'osmesa_link_args = []'
    new = "osmesa_link_args = ['-Wl,--build-id=sha1']"
    if old not in s:
        sys.exit('osmesa_link_args initialization not found, patch failed')
    s = s.replace(old, new, 1)
    open(p, 'w').write(s)
    print('patched osmesa meson: link with GNU build-id')
PYEOF

# KGSL Gallium backend sources (adapted to 25.0.7; see patches/kgsl/README.md).
if [ -d "$script_dir/patches/kgsl" ]; then
    mkdir -p "$mesadir/src/freedreno/drm/kgsl"
    cp "$script_dir"/patches/kgsl/*.c "$script_dir"/patches/kgsl/*.h "$mesadir/src/freedreno/drm/kgsl/"
fi

# ---------------------------------------------------------------------------
# Cross file
# ---------------------------------------------------------------------------
cat > "$workdir/android-aarch64-mesa.txt" <<EOF
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

# ---------------------------------------------------------------------------
# Configure & build
# ---------------------------------------------------------------------------
if [ ! -f "$builddir/build.ninja" ]; then
    rm -rf "$builddir"
    echo "Configuring Mesa $mesa_ver (OSMesa + freedreno/KGSL)..."
    meson setup "$builddir" "$mesadir" --wrap-mode=nofallback \
        --cross-file "$workdir/android-aarch64-mesa.txt" \
        -Dbuildtype=release -Dstrip=true \
        -Dplatforms=android -Dplatform-sdk-version="$sdkver" -Dandroid-stub=true \
        -Dgallium-drivers=freedreno,softpipe -Dvulkan-drivers= -Dfreedreno-kmds=kgsl \
        -Dosmesa=true -Dopengl=true \
        -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dglx=disabled -Dgbm=disabled \
        -Dllvm=disabled -Dlmsensors=disabled -Dvalgrind=disabled -Dlibunwind=disabled \
        -Dzstd=disabled -Db_lto=false \
        -Dc_args=-march=armv8.2-a+dotprod+fp16 -Dcpp_args=-march=armv8.2-a+dotprod+fp16
fi

echo "Compiling (-j$jobs)..."
ninja -C "$builddir" -j "$jobs"

src_so="$builddir/src/gallium/targets/osmesa/libOSMesa.so.8.0.0"
[ -f "$src_so" ] || src_so="$(find "$builddir" -name 'libOSMesa.so*' -type f | head -n1)"
[ -n "$src_so" ] && [ -f "$src_so" ] || { echo -e "$red libOSMesa not produced $nocolor"; exit 1; }

mkdir -p "$outdir"
cp "$src_so" "$outdir/libOSMesa_8.so"
"$ndk_bin/llvm-strip" --strip-unneeded "$outdir/libOSMesa_8.so"
patchelf --set-soname libOSMesa_8.so "$outdir/libOSMesa_8.so"

echo "== Build checks =="
"$ndk_bin/llvm-readelf" -h "$outdir/libOSMesa_8.so" | grep -q AArch64 && echo -e "$green - ELF: AArch64 $nocolor"
echo " - NEEDED: $("$ndk_bin/llvm-readelf" -d "$outdir/libOSMesa_8.so" | awk '/NEEDED/{print $NF}' | tr '\n' ' ')"
grep -aq "Adreno (TM) 730" "$outdir/libOSMesa_8.so" && echo -e "$green - GPU name: Adreno (TM) 730 $nocolor"
grep -aq "kgsl" "$outdir/libOSMesa_8.so" && echo -e "$green - KGSL backend present $nocolor"
grep -aq "freedreno" "$outdir/libOSMesa_8.so" && echo -e "$green - freedreno driver present $nocolor"
echo " - size: $(du -h "$outdir/libOSMesa_8.so" | cut -f1)"
sha256sum "$outdir/libOSMesa_8.so" | tee "$outdir/SHA256SUMS"
echo -e "$green Done: $outdir/libOSMesa_8.so $nocolor"
