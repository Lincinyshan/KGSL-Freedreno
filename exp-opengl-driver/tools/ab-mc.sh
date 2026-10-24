#!/bin/bash -e
# 无人值守 A/B 工具（配合 FCL 插件的 fcl_ab.txt 开关）
#
#   ADB="adb -s <serial>" ./ab-mc.sh env "<多行 KEY=VALUE>"
#   ADB=... ./ab-mc.sh shots <prefix> [张数] [间隔秒]
#   ADB=... ./ab-mc.sh flicker <prefix> [张数]
#
# shots/flicker 要求 MC 已经在世界里（游戏内）再执行。
SHOTS=${SHOTS:-/home/lcys/android/gpu-driver/.tmp-shots}
A=${ADB:-"adb -s 192.168.2.101:41293"}
mkdir -p "$SHOTS"
here="$(cd "$(dirname "$0")" && pwd)"

cmd=$1; shift || true
case "$cmd" in
  env)
    printf '%s\n' "$1" > /tmp/fcl_ab.txt
    $A push /tmp/fcl_ab.txt /sdcard/FCL/fcl_ab.txt >/dev/null
    echo "--- fcl_ab.txt:"; $A shell cat /sdcard/FCL/fcl_ab.txt
    $A shell am force-stop com.tungsten.fcl
    echo "--- FCL force-stopped（重新启动 FCL 才会生效）"
    ;;
  start)
    $A shell am start -n com.tungsten.fcl/.activity.SplashActivity >/dev/null
    echo started
    ;;
  shots)
    p=$1; n=${2:-8}; iv=${3:-0.25}
    $A shell "rm -f /sdcard/_ab_*.png"
    for i in $(seq 1 "$n"); do
      $A shell screencap -p /sdcard/_ab_$i.png
      sleep "$iv"
    done
    for i in $(seq 1 "$n"); do
      $A pull /sdcard/_ab_$i.png "$SHOTS/$p-$i.png" >/dev/null
    done
    echo "captured $n -> $SHOTS/$p-*.png"
    ;;
  flicker)
    p=$1; n=${2:-8}
    python3 "$here/flicker.py" "$SHOTS/$p" "$n"
    ;;
  *)
    sed -n '2,12p' "$0"
    ;;
esac
