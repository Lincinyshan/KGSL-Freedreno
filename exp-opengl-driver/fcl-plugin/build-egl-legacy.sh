#!/bin/bash -e
# 打包「26.3 适配前」的 EGL 插件：独立包名/渲染器名，可与正式版共存
script_dir="$(cd "$(dirname "$0")" && pwd)"
ndk="$script_dir/../../android-ndk-r30"
libdir="$script_dir/../.legacy/lib"
jar="$script_dir/android-29.jar"
name="MesaFreedrenoEGL-Legacy-A730-25.0.7"
cc="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
out="$script_dir/out-egl-legacy"

rm -rf "$out"; mkdir -p "$out/apk/lib/arm64-v8a"
cp "$libdir/libEGL_mesa_gl.so" "$libdir/libgallium_dri.so" "$out/apk/lib/arm64-v8a/"
"$cc" -O2 -shared -fPIC -Wl,-soname,libEGL_freedreno.so -Wl,-z,lazy -Wl,-z,max-page-size=16384 \
    -o "$out/apk/lib/arm64-v8a/libEGL_freedreno.so" "$script_dir/../tools/egl_bridge_shim.c" -ldl

cd "$script_dir"
aapt2 compile --dir res -o "$out/res.zip"
aapt2 link -o "$out/base.apk" -I "$jar" \
    --manifest AndroidManifest-egl-legacy.xml \
    --min-sdk-version 26 --target-sdk-version 34 "$out/res.zip"
( cd "$out/apk" && zip -qr ../base.apk lib )
zipalign -f 4 "$out/base.apk" "$out/aligned.apk"

certs="$SIGNING_CERTS_DIR"
[ -z "$certs" ] && certs="/home/lcys/文档/deepseek-harness/default-workspace/signing-certs"
p12="$certs/codesign/codesign.p12"
pw="$KEYSTORE_PASSWORD"
[ -z "$pw" ] && [ -f "$certs/passwords.txt" ] && pw="$(grep -m1 '^KEYSTORE_PASSWORD=' "$certs/passwords.txt" | cut -d= -f2-)"
apksigner sign --ks "$p12" --ks-type PKCS12 --ks-pass "pass:$pw" --key-pass "pass:$pw" \
    --ks-key-alias "local code signing" --out "$script_dir/$name.apk" "$out/aligned.apk"
echo "APK: $script_dir/$name.apk"
aapt dump badging "$script_dir/$name.apk" 2>/dev/null | head -1
