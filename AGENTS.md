# AGENTS.md

给接手这个仓库的编码 agent 的说明。**先读这一页，再读 `docs/HANDOVER.md`。**

## 这是什么

Mesa 26.3 的 EGL + **freedreno gallium 驱动** + **KGSL 后端**，打包成 FCL 的渲染器插件，
让 Minecraft Java 版跑桌面 OpenGL 4.6（ANativeWindow 直出，零拷贝）。

- 目标 GPU：Qualcomm Adreno 730v3，`chip_id = 0x07030002` → `fd_dev_info` 里是
  **`a7xx_gen1`**（不是 a6xx，虽然营销名是 730）
- 目标平台：Android + KGSL（`/dev/kgsl-3d0`，**没有 DRM 节点**）。已在 Android 15 /
  内核 5.10 的 Xiaomi Pad 6 Pro 上验证，理论上适用于同代 KGSL 设备
- 上游：Mesa 26.3.0-rc1。这是**下游 fork**，产物只做 FCL 插件，不往上游提 MR

## 目录

```
docs/                 文档（HANDOVER、UBWC 笔记、EGL 说明）
mesa/patches/         改过的上游文件「整文件快照」+ MAP（kgsl/ 另有 README/来源说明）
plugin/               FCL 插件：AndroidManifest.xml / java/ / res/ / build.sh
scripts/              setup-build.sh（meson 配置）、build-egl.sh（ninja → dist/）
tools/                gltest / ab-mc.sh / flicker.py / egl_bridge_shim.c
dist/                 产物（gitignore）
```

构建工作区（Mesa 源码、NDK、deps、meson build 目录）**都不入库**，位置由环境变量决定。

## 构建

依赖：

| 组件 | 说明 |
|---|---|
| Mesa 源码 | 26.3.0-rc1（`MESA_SRC` 指定） |
| Android NDK | r30，含 API 35 sysroot（`NDK_DIR` 指定） |
| Android SDK | `build-tools`（`aapt2` / `apksigner` / `zipalign`） |
| meson / ninja | `meson` ≥ 1.3 |
| 可选 | 静态 `libdrm`（`DEPS_DIR`，见下） |

```bash
export MESA_SRC=/path/to/mesa-26.3.0-rc1
export NDK_DIR=/path/to/android-ndk-r30
export BUILD_DIR=/path/to/build/mesa-26.3      # 可选，默认 ./build/mesa-26.3
scripts/setup-build.sh                          # 生成 cross file + meson setup

scripts/build-egl.sh                            # ninja → dist/libEGL_mesa_gl.so, dist/libgallium_dri.so
cd plugin && ./build.sh                         # → dist/MesaFreedreno-26.3-main.apk
```

- 插件构建依赖 `plugin/android-29.jar`（Android SDK 里的 `android.jar`）和
  `tools/r8.jar`（Google Maven `com.android.tools:r8`）。两者都不入库，缺失时脚本会报错。
- 签名用 `SIGNING_CERTS_DIR` 指向的 `codesign/codesign.p12`（口令从同目录
  `passwords.txt` 读）；没有就用 `plugin/debug.keystore` 调试签名。**不要提交证书/口令**。
- 产物全部在 `dist/`（gitignore），构建不会产生 git 变更。
- 只有用户明确要求时才构建。

## 插件契约

| 项 | 值 |
|---|---|
| package | `pkg.freedreno.kgsl`（单一包名） |
| 版本 | `26.3.0-main`（只有一个版本，不要 release/debug/ubwc 变体） |
| FCL 显示名 | `des` 和 `renderer` 首段都是 `KGSL-Freedreno（OpenGL 4.6）`（全角括号） |
| 应用名 | `Mesa Freedreno A730 (26.3 UBWC)` |
| MC 版本 | 不限：`minMCVer`/`maxMCVer` 留空 |
| 图标 | 方案 B（芯片 + 穿透箭头），由 `plugin/icon/make-icon.py` 生成，勿手改 PNG |

**设置通道**：Activity 写 `Android/media/pkg.freedreno.kgsl/fcl_env.txt`，驱动在
`dri2_initialize_android()` 里由 `fcl_load_ab_config()` 读取并 `setenv`。
不要恢复旧的 `/sdcard/FCL/fcl_ab.txt` 通道。

`ubwc / notile / flush / sysmem / nobin / FCL_NO_FAST_CLEAR` 是运行时开关，不是打包变体。

## 改 Mesa 的规矩

1. 先读代码再改；改完把**整个文件**拷进 `mesa/patches/`，新增文件要同时加进 `mesa/patches/MAP`。
2. KGSL 后端是 SolDev69/freedreno-gallium-mesa（24.1.2）原版，只保留 3 处 26.x 适配
   （`dev->features`、`.reset_status`、suballoc IB 地址），见 `mesa/patches/kgsl/README.md`。
   不要再加自研的 pending 释放队列 / 强制 IOCOHERENT / cache 维护，除非有实测证据。
3. 改 flush / CCU / UBWC 时 **a6xx 和 a7xx 两条分支都要看**：目标机是 a7xx，
   但代码里同时存在 `CHIP == A6XX` 分支。
4. 不要在 `libgallium_dri` 里留调试 `fprintf`（EGL 层的 `[fcl-dri]` 前缀日志可以留）。

## 上机调试

- 用 `ANDROID_SERIAL=...` 指定设备，不要硬编码序列号；先 `adb devices`。
- 装完插件、改完设置、换完库：必须 `adb shell am force-stop com.tungsten.fcl`。
- 自检：`tools/gltest`（`UBWC-*` 用例，logcat 标签 `GLTEST`）。
- A/B：`ANDROID_SERIAL=... tools/ab-mc.sh env "<KEY=VALUE...>"`；`tools/flicker.py` 算闪烁指标。
- 判据：截图后裁热栏 `convert shot.png -crop 1300x140+880+1650 +repage -resize 300%`。

## 提交风格

- commit message 用中文，一个主题一个提交；打包时把 APK 的 sha256 写进去。
- 不提交二进制 / 证书 / 构建产物（`dist/`、`archive/`、`signing-certs/` 已 gitignore）。
- 不改写历史、不强制 push。

## 已知坑

| 现象 | 原因 |
|---|---|
| `cannot get version` / screen 建不起来 | KGSL 没有 DRM 节点，`drmGetVersion()` 返回 NULL → 必须回退 `kgsl_device_new()` |
| `create_screen: dd=kmsro` | 驱动名被解析成 kmsro；要 `driver_name = "freedreno"` 并覆盖 `LIBGL_DRIVERS_PATH` |
| 驱动加载失败 `libdrm.so not found` | 26.3 产物 NEEDED libdrm；静态链接或一起打包 |
| `eglCreateContext(4.6 core)` 返回 0 / `0x3009` | 没先 `eglBindAPI(EGL_OPENGL_API)`，config 没带 `EGL_OPENGL_BIT` |
| 只有 UBWC 下花屏 | a7xx 需要显式 `FD_CCU_CLEAN_BLIT_CACHE`；KGSL 没有隐式同步 |
