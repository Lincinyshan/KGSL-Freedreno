#!/bin/bash -e
# ⚠️ 已冻结，不再维护：EGL 直出路线见 build-egl-plugin.sh / README-FREEDRENO-EGL.md
# Package the Mesa freedreno OSMesa library into an FCL (FoldCraftLauncher)
# renderer-plugin APK.
#
#   lib   : release-25.0.7-a730/libOSMesa_8.so   (或 LIB=...)
#   jar   : android-29.jar                      (或 ANDROID_JAR=...)
#   res   : res/mipmap-*/ic_launcher.png        (由 icon/make-icon.py 生成)
#   sign  : 个人代码签名证书（codesign.p12），缺失时回退到 debug keystore
#
# 签名证书目录可用 SIGNING_CERTS_DIR 覆盖，口令用 KEYSTORE_PASSWORD 覆盖，
# 否则自动读取目录下的 passwords.txt。
script_dir="$(cd "$(dirname "$0")" && pwd)"
lib="$script_dir/../release-25.0.7-a730/libOSMesa_8.so"
jar="$script_dir/android-29.jar"
out="$script_dir/out"
name="MesaFreedreno-A730-25.0.7"

if [ -n "$LIB" ]; then lib="$LIB"; fi
if [ -n "$ANDROID_JAR" ]; then jar="$ANDROID_JAR"; fi

[ -f "$lib" ] || { echo "missing $lib"; exit 1; }
[ -f "$jar" ] || { echo "missing android.jar at $jar"; exit 1; }

rm -rf "$out"
mkdir -p "$out/apk/lib/arm64-v8a"
cp "$lib" "$out/apk/lib/arm64-v8a/libOSMesa_8.so"

cd "$script_dir"

# 1) resources (launcher icon)
aapt2 compile --dir res -o "$out/res.zip"
aapt2 link -o "$out/base.apk" -I "$jar" \
    --manifest AndroidManifest.xml \
    --min-sdk-version 26 --target-sdk-version 34 \
    "$out/res.zip"

# 2) native lib
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
aapt dump badging "$script_dir/$name.apk" 2>/dev/null | head -6 || true
apksigner verify --print-certs "$script_dir/$name.apk" 2>/dev/null | head -3 || true
unzip -l "$script_dir/$name.apk"
