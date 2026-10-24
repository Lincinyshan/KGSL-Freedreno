#!/bin/bash -e
# 打包 EGL 直出版 FCL 插件：shim(libEGL_freedreno.so) + 真 libEGL + libgallium_dri
#
#   lib   : release-25.0.7-a730-egl/{libEGL_mesa_gl.so,libgallium_dri.so}
#   res   : res/mipmap-*/ic_launcher.png（由 icon/make-icon.py 生成）
#   sign  : signing-certs/codesign/codesign.p12，缺失时回退 debug keystore
#
# 证书目录可用 SIGNING_CERTS_DIR 覆盖，口令用 KEYSTORE_PASSWORD 覆盖，
# 否则自动读取目录下的 passwords.txt。
script_dir="$(cd "$(dirname "$0")" && pwd)"
ndk="$script_dir/../../android-ndk-r30"; [ -n "$NDK_DIR" ] && ndk="$NDK_DIR"
libdir="$script_dir/../release-25.0.7-a730-egl"; [ -n "$LIBDIR" ] && libdir="$LIBDIR"
jar="$script_dir/android-29.jar"; [ -n "$ANDROID_JAR" ] && jar="$ANDROID_JAR"
out="$script_dir/out-egl"
name="MesaFreedrenoEGL-A730-25.0.7"
cc="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"

[ -f "$libdir/libEGL_mesa_gl.so" ] || { echo "missing $libdir/libEGL_mesa_gl.so"; exit 1; }
[ -f "$libdir/libgallium_dri.so" ] || { echo "missing $libdir/libgallium_dri.so"; exit 1; }
[ -f "$jar" ] || { echo "missing $jar"; exit 1; }

rm -rf "$out"; mkdir -p "$out/apk/lib/arm64-v8a"
cp "$libdir/libEGL_mesa_gl.so" "$libdir/libgallium_dri.so" "$out/apk/lib/arm64-v8a/"
"$cc" -O2 -shared -fPIC -Wl,-soname,libEGL_freedreno.so \
    -o "$out/apk/lib/arm64-v8a/libEGL_freedreno.so" "$script_dir/../tools/egl_bridge_shim.c" -ldl

cd "$script_dir"

# 1) resources (launcher icon)
aapt2 compile --dir res -o "$out/res.zip"
aapt2 link -o "$out/base.apk" -I "$jar" \
    --manifest AndroidManifest-egl.xml \
    --min-sdk-version 26 --target-sdk-version 34 \
    "$out/res.zip"

# 2) native libs
( cd "$out/apk" && zip -qr ../base.apk lib )
zipalign -f 4 "$out/base.apk" "$out/aligned.apk"

# 3) signing
certs="$SIGNING_CERTS_DIR"
if [ -z "$certs" ]; then
    certs="/home/lcys/文档/deepseek-harness/default-workspace/signing-certs"
fi
p12="$certs/codesign/codesign.p12"
pw="$KEYSTORE_PASSWORD"
if [ -z "$pw" ] && [ -f "$certs/passwords.txt" ]; then
    pw="$(grep -m1 '^KEYSTORE_PASSWORD=' "$certs/passwords.txt" | cut -d= -f2-)"
fi

if [ -f "$p12" ] && [ -n "$pw" ]; then
    apksigner sign --ks "$p12" --ks-type PKCS12 \
        --ks-pass "pass:$pw" --key-pass "pass:$pw" \
        --ks-key-alias "local code signing" \
        --out "$script_dir/$name.apk" "$out/aligned.apk"
    echo "signed with codesign.p12 (alias 'local code signing')"
else
    echo "WARN: codesign.p12 / password not found, falling back to debug keystore"
    if [ ! -f "$script_dir/debug.keystore" ]; then
        keytool -genkeypair -keystore "$script_dir/debug.keystore" -storepass android \
            -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 \
            -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
    fi
    apksigner sign --ks "$script_dir/debug.keystore" --ks-pass pass:android \
        --key-pass pass:android --ks-key-alias androiddebugkey \
        --out "$script_dir/$name.apk" "$out/aligned.apk"
fi

echo "APK: $script_dir/$name.apk"
aapt dump badging "$script_dir/$name.apk" 2>/dev/null | head -4 || true
apksigner verify --print-certs "$script_dir/$name.apk" 2>/dev/null | head -3 || true
unzip -l "$script_dir/$name.apk"
