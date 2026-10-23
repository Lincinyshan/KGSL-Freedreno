#!/bin/bash -e
# 交叉编译设备端 OSMesa 自检程序（不依赖 adb）。
script_dir="$(cd "$(dirname "$0")" && pwd)"
ndk="$script_dir/../../android-ndk-r30"; [ -n "$NDK_DIR" ] && ndk="$NDK_DIR"
libdir="$script_dir/../release-25.0.7-a730"; [ -n "$LIBDIR" ] && libdir="$LIBDIR"
sdkver="$DEVICE_SDK"; [ -z "$sdkver" ] && sdkver=35
cc="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$sdkver-clang"
out="$script_dir/out"

[ -x "$cc" ] || { echo "missing ndk clang: $cc"; exit 1; }
[ -f "$libdir/libOSMesa_8.so" ] || { echo "missing $libdir/libOSMesa_8.so"; exit 1; }

mkdir -p "$out"
"$cc" -O2 -Wall -o "$out/osmesa_probe" "$script_dir/osmesa_probe.c" \
    -L"$libdir" -l:libOSMesa_8.so -Wl,--build-id=sha1
cp "$libdir/libOSMesa_8.so" "$out/"
echo "built: $out/osmesa_probe"
echo "打包目录: $out/  (osmesa_probe + libOSMesa_8.so)"
