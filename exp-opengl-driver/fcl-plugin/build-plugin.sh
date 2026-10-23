#!/bin/bash -e
# Package the Mesa freedreno OSMesa library into an FCL (FoldCraftLauncher)
# renderer-plugin APK.  Requires android.jar (API 34/35) in this directory,
# build-tools (aapt2/zipalign/apksigner) and a local debug keystore.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
lib="${LIB:-$script_dir/../release-25.0.7-a730/libOSMesa_8.so}"
jar="${ANDROID_JAR:-$script_dir/android-29.jar}"
out="$script_dir/out"
name="MesaFreedreno-A730-25.0.7"

[ -f "$lib" ] || { echo "missing $lib"; exit 1; }
[ -f "$jar" ] || { echo "missing android.jar at $jar"; exit 1; }

rm -rf "$out"
mkdir -p "$out/apk/lib/arm64-v8a"
cp "$lib" "$out/apk/lib/arm64-v8a/libOSMesa_8.so"

cd "$script_dir"
aapt2 link -o "$out/base.apk" -I "$jar" \
    --manifest AndroidManifest.xml \
    --min-sdk-version 26 --target-sdk-version 34

( cd "$out/apk" && zip -qr ../base.apk lib )
zipalign -f 4 "$out/base.apk" "$out/aligned.apk"

if [ ! -f "$script_dir/debug.keystore" ]; then
    keytool -genkeypair -keystore "$script_dir/debug.keystore" -storepass android \
        -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 \
        -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
fi

apksigner sign --ks "$script_dir/debug.keystore" --ks-pass pass:android \
    --key-pass pass:android --ks-key-alias androiddebugkey \
    --out "$script_dir/$name.apk" "$out/aligned.apk"

echo "APK: $script_dir/$name.apk"
aapt dump badging "$script_dir/$name.apk" 2>/dev/null | head -5 || true
unzip -l "$script_dir/$name.apk"
