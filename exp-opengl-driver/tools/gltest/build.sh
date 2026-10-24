#!/bin/bash -e
# 独立复现 APK：NativeActivity + 我们的 libEGL/libgallium_dri
script_dir="$(cd "$(dirname "$0")" && pwd)"
ndk="$script_dir/../../../android-ndk-r30"; [ -n "$NDK_DIR" ] && ndk="$NDK_DIR"
libdir="$script_dir/../../release-25.0.7-a730-egl"; [ -n "LIBDIR" ] && true
jar="$script_dir/../../fcl-plugin/android-29.jar"; [ -n "$ANDROID_JAR" ] && jar="$ANDROID_JAR"
out="$script_dir/out"
cc="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
bt=/usr/lib/android-sdk/build-tools/29.0.3

mkdir -p "$out/apk/lib/arm64-v8a"

# 复制插件 shim（若已构建）
shimdir="$script_dir/../../fcl-plugin/out-egl/apk/lib/arm64-v8a"
cp "$libdir/libEGL_mesa_gl.so" "$libdir/libgallium_dri.so" "$out/apk/lib/arm64-v8a/"
[ -f "$shimdir/libEGL_freedreno.so" ] && cp "$shimdir/libEGL_freedreno.so" "$out/apk/lib/arm64-v8a/"

"$cc" -O2 -shared -fPIC -Wl,-soname,libgltest.so \
    -o "$out/apk/lib/arm64-v8a/libgltest.so" \
    "$script_dir/main.c" "$ndk/sources/android/native_app_glue/android_native_app_glue.c" \
    -I"$ndk/sources/android/native_app_glue" \
    -landroid -llog -lnativewindow -ldl -u ANativeActivity_onCreate

cd "$out/apk"
"$bt/aapt2" link -o "$out/base.apk" -I "$jar" \
    --manifest "$script_dir/AndroidManifest.xml" \
    --min-sdk-version 29 --target-sdk-version 29
cd "$out"
rm -rf pkg && mkdir pkg && cd pkg
unzip -qo ../base.apk
cp -r ../apk/lib .
zip -qr ../unsigned.apk .
cd ..
"$bt/zipalign" -f 4 unsigned.apk aligned.apk
"$bt/apksigner" sign --ks "$script_dir/../../fcl-plugin/debug.keystore" \
    --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey \
    --out "$out/EGLTest.apk" aligned.apk
echo "built $out/EGLTest.apk"
