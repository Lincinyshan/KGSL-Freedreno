#!/bin/bash
# 判断 FCL 游戏日志里 OpenGL(EGL 插件) 后端是否真的启用。
#
#   ./check-game-log.sh              # 自动从 adb 拉 latest_game.log
#   ./check-game-log.sh 日志文件      # 直接分析本地文件
#
# 退出码：0=OpenGL 成功  1=失败(附原因)  2=日志不可用/未判定
set -u

log="${1:-}"
if [ -z "$log" ]; then
    log="$(mktemp)"
    adb pull /storage/emulated/0/FCL/log/latest_game.log "$log" >/dev/null 2>&1 || {
        echo "无法从设备拉取日志（adb 连上了吗？）"; exit 2; }
fi
[ -s "$log" ] || { echo "日志为空：$log"; exit 2; }

echo "== 关键行 =="
grep -nE 'Backend library|Using graphics backend|Failed to create backend|Could not retrieve EGL function|Failed to dynamically load library|fcl-egl\] (KGSL|dmabuf|create_window_surface|shim loaded)' "$log" | head -20

# 插件 lib 目录是否陈旧（覆盖安装后没重启 FCL 的典型症状）
libpath="$(grep -oE '/data/app/[^ )"]*com\.a730\.mesa\.egl[^ )"]*/lib/arm64/libEGL_freedreno\.so' "$log" | head -1)"
stale_hint=""
if [ -n "$libpath" ] && command -v adb >/dev/null 2>&1; then
    if ! adb shell "test -f '$libpath'" >/dev/null 2>&1; then
        stale_hint="插件目录已失效：$libpath
   → 多半是『装了新 APK 但没重启 FCL』：FCL 缓存了旧插件路径。
     先 adb shell am force-stop com.tungsten.fcl，再重开 FCL 启动游戏。"
    fi
fi

if grep -q 'Using graphics backend OpenGL' "$log"; then
    echo
    echo "结果：✅ OpenGL 后端已启用（EGL 插件生效）"
    exit 0
fi

if grep -q 'Failed to dynamically load library' "$log"; then
    echo
    echo "结果：❌ 插件 .so 没加载成功"
    grep -m1 'Failed to dynamically load library' "$log"
    [ -n "$stale_hint" ] && { echo; echo "$stale_hint"; }
    exit 1
fi

if grep -q 'Failed to create backend OpenGL\|Could not retrieve EGL function' "$log"; then
    echo
    echo "结果：❌ OpenGL 后端创建失败，游戏回退了其它后端"
    echo "原因行："
    grep -m2 -E 'Failed to create backend OpenGL|Could not retrieve EGL function' "$log"
    [ -n "$stale_hint" ] && { echo; echo "$stale_hint"; }
    exit 1
fi

if grep -q 'Using graphics backend Vulkan' "$log"; then
    echo
    echo "结果：⚠️ 游戏用了 Vulkan（不是我们的 EGL 路径）"
    [ -n "$stale_hint" ] && { echo; echo "$stale_hint"; }
    exit 1
fi

echo
echo "结果：❓ 日志里没有渲染后端信息（可能还没跑到建窗口，或不是这次运行的日志）"
[ -n "$stale_hint" ] && { echo; echo "$stale_hint"; }
exit 2
