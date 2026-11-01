# 交接文档 — Mesa freedreno (KGSL) FCL 插件

> 状态：**v1.0.2** · Mesa **26.3.0-rc3** · 2026-11-01
> 面向：接手这个仓库的人 / agent。先读本文件，再看 `README.md` 与 `AGENTS.md`。

> [!WARNING]
> 本文结论**随时可能被推翻**：它们只是写这份文档时最强的证据。后续若出现新日志或新代码依据，以**实测与源码**为准，并直接更新文档（提交信息里写清依据）。

## 1. 现在是什么状态

一个给 FCL 用的 **OpenGL 驱动插件**：Mesa 26.3 的 EGL + freedreno gallium 驱动 +
KGSL 后端，打包成 APK，FCL 里显示为 **KGSL-Freedreno（OpenGL 4.6）**。

**已在真机验证可用**：原版 Minecraft 26.3、Iris 光影（Chocapic13 V6 Lite）、
物品图标、区块渲染都正常。核心崩溃（`Failed to wait for frame completion`）已修复。

## 2. 版本与产物

| 项 | 值 |
|---|---|
| 包名 | `pkg.freedreno.kgsl` |
| 版本 | `1.0.2` / `versionCode 3`（tag `v1.0.2`） |
| APK | `dist/MesaFreedreno-26.3-main.apk` · sha256 `24a5660e5bcee055c5c0892ea26dc8759353e02220f81180abf74a3c2b7ff38c` |
| `libgallium_dri.so` | sha256 `3a09a6cd038d891fba9fb066422cbc1e8d0a7f407dbbc32b97a8e0d3f785e217` |
| `libEGL_mesa_gl.so` | sha256 `64c1f8ab56512475186f4cbc49adaab329f11abe39ad7e569179c776a106226e` |
| 签名证书 SHA-256 | `66ab6f193ed3e6d6637d66e0b004a38505b00056f0b7b127da5f1422563362b8` |

版本规则：**versionCode 每次发布会 +1**，versionName 语义化（1.0.1 → 1.0.2 → 1.1 → 2.0），
只有一个发布线（main），没有 debug/release/ubwc 变体。打包后打 tag `vX.Y.Z`。

## 3. 设备与支持范围

- 目标设备：Xiaomi Pad 6 Pro（SM8475），**Adreno 730v3**，`chip_id = 0x07030002`
  → `freedreno_devices.py` 里是 `a7xx_gen1`（营销名 730）
- Android 15 / 内核 5.10 / KGSL（`/dev/kgsl-3d0`，**没有 DRM 节点**）
- **支持范围 = Mesa 上游 freedreno 支持范围**：设备表里有、且是 KGSL 平台就能跑
  （6xx/7xx/8xx 同一份 `fd6_*` 驱动）。只有 arm64，不支持纯 msm DRM 设备。
- 其他机型不支持时：把 logcat / 游戏日志交给 AI，让它开 fork 适配（本项目就是这么来的）。

## 4. 仓库结构

```text
README.md / AGENTS.md / LICENSE
docs/                  HANDOVER（本文件）、UBWC 笔记、EGL 说明
mesa/patches/          改过的上游文件「整文件快照」+ MAP + kgsl/（附来源与适配说明）
plugin/                FCL 插件：AndroidManifest.xml / java/ / res/ / icon/ / build.sh
scripts/setup-build.sh 生成 cross file + meson setup（MESA_SRC / NDK_DIR / DEPS_DIR / BUILD_DIR）
scripts/build-egl.sh   ninja → dist/
tools/                 gltest / ab-mc.sh / flicker.py / egl_bridge_shim.c
dist/                  产物（不入库）
archive/               旧版本/旧清单/日志（不入库）
```

构建工作区（Mesa 源码、NDK、deps、meson build 目录）**都不入库**，由环境变量决定；
本机路径写在仓库根的 `.env.local`（也不入库）。

## 5. 构建与发布

```bash
export MESA_SRC=/path/to/mesa-26.3.0-rc3
export NDK_DIR=/path/to/android-ndk-r30
export DEPS_DIR=/path/to/static-deps        # 可选：静态 libdrm
export BUILD_DIR=/path/to/build/mesa-26.3   # 可选，默认 ./build/mesa-26.3
scripts/setup-build.sh

scripts/build-egl.sh                        # → dist/libEGL_mesa_gl.so, dist/libgallium_dri.so
cd plugin && ./build.sh                     # → dist/MesaFreedreno-26.3-main.apk
```

- 插件构建依赖 `plugin/android-29.jar` 和 `tools/r8.jar`（都不入库）；
- 签名用 `SIGNING_CERTS_DIR` 指向的 `codesign/codesign.p12`，没有则回退 debug keystore；
- `--wrap-mode=nodownload` 已经写进 setup 脚本（freedreno 里 libarchive 是 optional，
  但 meson 会尝试下载 subproject，离线会把 setup 卡十几分钟）。

## 6. 关键实现

| 位置 | 作用 |
|---|---|
| `tools/egl_bridge_shim.c` | 导出完整 EGL ABI 的 shim（62 个入口）；强制桌面 GL 4.6；`FCL_GL_FORCE_CORE/COMPAT` 覆盖 profile |
| `mesa/patches/platform_android.c` | 没有 DRM 节点时 open `/dev/kgsl-3d0`、覆盖 `LIBGL_DRIVERS_PATH`、读 `fcl_env.txt` |
| `mesa/patches/freedreno_device.c` | `drmGetVersion()` 失败回退 `kgsl_device_new()` |
| `mesa/patches/kgsl/` | KGSL gallium 后端（fork 24.1.2），**4 处适配**见其 README |
| `plugin/java/.../MainActivity.java` | 设置界面，写 `Android/media/pkg.freedreno.kgsl/fcl_env.txt` |

设置通道：Activity 写文件 → `fcl_load_ab_config()` 在 EGL 初始化时 `setenv` →
**必须 force-stop FCL** 才生效。开关：`flush/notile/noubwc/sysmem/nobin`、
`FCL_NO_FAST_CLEAR`、`FCL_GL_FORCE_CORE/COMPAT`。

## 7. 踩过的坑与修复（按时间）

1. **上游 Gallium 没有 KGSL 后端** → 从 fork 回移，只留 4 处 26.x 适配；
2. **EGL 拿不到 DRM 设备** → `drmGetVersion()` 失败时回退 KGSL、驱动名 `freedreno`；
3. **26.3 产物 NEEDED libdrm** → 一起打包 / 静态链；
4. **Core 下 Iris 的 `lines` 着色器失败** → 那是 `#version 150 compatibility`，
   Core 下无解，需切 Compatibility（Chocapic13 等包不受影响）；
5. **物品图标噪点/透明、`Failed to wait for frame completion`** → 根因是
   `kgsl_pipe_wait()` 把**纳秒**超时当**毫秒**（放大 100 万倍），v1.0.1 修复；
   修复后 Core + 光影 + UBWC 也一并干净；
6. **区块锯齿空洞** → 不是驱动 bug，是原版「区块构建器：线程化」的已知行为；
7. **升级插件后首启崩溃**（`libEGL_freedreno.so not found`）→ FCL 缓存了旧
   `/data/app/~~<hash>` 路径，装完必须 force-stop。

## 8. 已知问题

- 个别光影包需要 Compatibility Profile（见上）；
- 启动初期（区块加载 + JIT + GC）会卡，2–3 分钟后恢复，属于 MC/Java 侧；
- 其他 GPU 未实机验证。

## 9. 调试速查

```bash
adb devices                              # 序列号每次可能变
adb install -r dist/MesaFreedreno-26.3-main.apk
adb shell am force-stop com.tungsten.fcl # 装插件/改设置/换库后必做
adb shell pidof com.tungsten.fcl
adb shell top -H -b -n 1 -p <pid>        # 看是 GC/JIT 还是渲染线程忙
```

- 自检：`tools/gltest`（`UBWC-*`）；
- A/B：`ADB="adb -s <serial>" tools/ab-mc.sh env "KEY=VALUE"`；
- 闪烁度量：`tools/flicker.py`；
- 截图判据：`convert shot.png -crop 1300x140+880+1650 +repage -resize 300%`。

## 10. 历史（简要）

- 2026-10 上旬：Turnip（Vulkan）侧工作，产出自用 Turnip 包；
- 2026-10 中旬：OSMesa + freedreno/KGSL 方案（已被放弃，25.0.7 归档在 `archive/`）；
- 2026-10 下旬：改成 **EGL 直出**（shim + Mesa EGL + gallium），26.3 回移 KGSL 后端；
- 2026-10-31：定位并修复 fence 超时单位 bug，发布 1.0.1；
- 2026-11-01：升级 Mesa 26.3.0-rc3，发布 1.0.2（包名/设置通道/文档整理完成）。

> 旧版交接文档（描述迁移前的 Turnip 构建）在 `vulkan-driver/HANDOVER.md`，仅作历史参考。
