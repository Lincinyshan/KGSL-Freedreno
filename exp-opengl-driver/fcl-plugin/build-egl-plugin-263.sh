#!/bin/bash -e
# 打包 EGL 直出版 FCL 插件：shim(libEGL_freedreno.so) + 真 libEGL + libgallium_dri
#
#   lib   : release-26.3.0-rc1-a730-egl/{libEGL_mesa_gl.so,libgallium_dri.so}
#   res   : res/mipmap-*/ic_launcher.png（由 icon/make-icon.py 生成）
#   sign  : signing-certs/codesign/codesign.p12，缺失时回退 debug keystore
#
# 证书目录可用 SIGNING_CERTS_DIR 覆盖，口令用 KEYSTORE_PASSWORD 覆盖，
# 否则自动读取目录下的 passwords.txt。
script_dir="$(cd "$(dirname "$0")" && pwd)"
ndk="$script_dir/../../android-ndk-r30"; [ -n "$NDK_DIR" ] && ndk="$NDK_DIR"
libdir="$script_dir/../release-26.3.0-rc1-a730-egl"; [ -n "$LIBDIR" ] && libdir="$LIBDIR"
jar="$script_dir/android-29.jar"; [ -n "$ANDROID_JAR" ] && jar="$ANDROID_JAR"
out="${OUT_DIR:-$script_dir/out-egl263}"
name="${APK_NAME:-MesaFreedrenoEGL-A730-26.3rc1}"
manifest="${MANIFEST_OVERRIDE:-AndroidManifest-egl263.xml}"  # MANIFEST_OVERRIDE 可指定 release 版清单
cc="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"

[ -f "$libdir/libEGL_mesa_gl.so" ] || { echo "missing $libdir/libEGL_mesa_gl.so"; exit 1; }
[ -f "$libdir/libgallium_dri.so" ] || { echo "missing $libdir/libgallium_dri.so"; exit 1; }
[ -f "$jar" ] || { echo "missing $jar"; exit 1; }

rm -rf "$out"; mkdir -p "$out/apk/lib/arm64-v8a"
cp "$libdir/libEGL_mesa_gl.so" "$libdir/libgallium_dri.so" "$out/apk/lib/arm64-v8a/"
# Mesa 26.3 的产物会 NEEDED libdrm.so，而 app 的 linker namespace 里没有它，
# 有就一起打包（从设备的 /system/lib64 拉下来的那份）。
[ -f "$libdir/libdrm.so" ] && cp "$libdir/libdrm.so" "$out/apk/lib/arm64-v8a/"
"$cc" -O2 -shared -fPIC -Wl,-soname,libEGL_freedreno.so -Wl,-z,lazy -Wl,-z,max-page-size=16384 \
    -o "$out/apk/lib/arm64-v8a/libEGL_freedreno.so" "$script_dir/../tools/egl_bridge_shim.c" -ldl

# 校验 shim 导出的 EGL 入口：新版本游戏会逐个 dlsym，缺一个就判定
# "OpenGL is not supported: Could not retrieve EGL function <名字>" 并回退 Vulkan。
readelf_bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
[ -x "$readelf_bin" ] || readelf_bin=readelf
exported="$($readelf_bin --dyn-syms "$out/apk/lib/arm64-v8a/libEGL_freedreno.so" | awk 'NF>=8 {print $8}' | tr '\n' ' ')"
required="eglBindAPI eglBindTexImage eglChooseConfig eglClientWaitSync eglCopyBuffers
 eglCreateContext eglCreateImage eglCreatePbufferFromClientBuffer eglCreatePbufferSurface
 eglCreatePixmapSurface eglCreatePlatformPixmapSurface eglCreatePlatformWindowSurface
 eglCreateSync eglCreateWindowSurface eglDestroyContext eglDestroyImage eglDestroySurface
 eglDestroySync eglGetConfigAttrib eglGetConfigs eglGetCurrentContext eglGetCurrentDisplay
 eglGetCurrentSurface eglGetDisplay eglGetError eglGetPlatformDisplay eglGetProcAddress
 eglGetSyncAttrib eglInitialize eglMakeCurrent eglQueryAPI eglQueryContext eglQueryString
 eglQuerySurface eglReleaseTexImage eglReleaseThread eglSurfaceAttrib eglSwapBuffers
 eglSwapInterval eglTerminate eglWaitClient eglWaitGL eglWaitNative eglWaitSync
 eglGetPlatformDisplayEXT eglCreatePlatformWindowSurfaceEXT eglCreatePlatformPixmapSurfaceEXT
 eglSwapBuffersWithDamageEXT eglSwapBuffersWithDamageKHR eglCreateImageKHR eglDestroyImageKHR
 eglCreateSyncKHR eglDestroySyncKHR eglClientWaitSyncKHR eglWaitSyncKHR eglGetSyncAttribKHR
 eglPresentationTimeANDROID eglGetNativeClientBufferANDROID eglCreateNativeClientBufferANDROID
 eglQueryDeviceStringEXT glXGetProcAddress glXGetProcAddressARB"
missing=""
for sym in $required; do
    case " $exported " in
        *" $sym "*) ;;
        *) missing="$missing $sym" ;;
    esac
done
if [ -n "$missing" ]; then
    echo "ERROR: shim 缺少必需 EGL 导出:$missing"
    exit 1
fi
echo "shim exports OK: $(echo $required | wc -w) required symbols present"

cd "$script_dir"

# 1) resources (launcher icon)
aapt2 compile --dir res -o "$out/res.zip"
aapt2 link -o "$out/base.apk" -I "$jar" \
    --manifest "$manifest" \
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
