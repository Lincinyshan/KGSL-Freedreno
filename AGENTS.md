# AGENTS.md

给接手这个仓库的编码 agent 的说明。**先读这一页，再读 `docs/HANDOVER.md`。**

## 这是什么

Mesa 26.3 的 EGL + **freedreno gallium 驱动** + **KGSL 后端**，打包成 FCL 的渲染器插件，
给 Minecraft Java 版跑桌面 OpenGL 4.6（ANativeWindow 直出，零拷贝）。

- 设备：Xiaomi Pad 6 Pro，Adreno 730v3，`chip_id = 0x07030002` → `a7xx_gen1`
  （**不是 a6xx**，虽然营销名叫 730；`fd_dev_info` 里走 `CHIP.A7XX`）
- 系统：Android 15 / API 35，内核 5.10 KGSL（`/dev/kgsl-3d0`，**没有 DRM 节点**）
- 上游：Mesa 26.3.0-rc1 源码树在 `vulkan-driver/Mesa-Turnip-Builder/turnip_workdir/mesa-mesa-26.3.0-rc1`
  （未跟踪，不要提交）

## 先读什么

| 文件 | 内容 |
|---|---|
| `docs/HANDOVER.md` | 项目历史、踩过的坑、结论 |
| `docs/NOTES-ubwc-artifacts.md` | UBWC 花屏排查现状（已修/已排除/仍开着） |
| `docs/NOTES-turnip-vs-gallium-ubwc.md` | 与 Turnip 的 UBWC 实现差异对照 |
| `mesa/patches/kgsl/README.md` | KGSL 后端来源与允许的适配 |
| `README.md` | 目录总览与构建入口 |

## 目录：哪些能动、哪些不能

```
docs/                 文档
mesa/patches/         改过的上游文件「整文件快照」+ MAP（kgsl/ 另有 README）
plugin/               FCL 插件：AndroidManifest.xml / java/ / res/ / build.sh
scripts/build-egl.sh  ninja 构建 → dist/
tools/                gltest / ab-mc.sh / flicker.py / egl_bridge_shim.c
dist/                 产物（gitignore）
archive/              旧版本、旧清单、日志（gitignore）
```

**不能改名/移动：**

- `exp-opengl-driver/gl_workdir/` —— meson 构建工作区。`build.ninja` 里有 1100+ 处
  **绝对路径**引用它，改名会让构建目录直接失效（要重新 `meson setup`）。
- `vulkan-driver/` —— Turnip 构建器 + Mesa 源码树，属于外部检出的上游代码。

**不要碰：** `archive/`、`third_party/`、`android-ndk-r30/`、`android-sdk/`。

## 插件契约（别改回去）

| 项 | 值 |
|---|---|
| package | `pkg.freedreno.kgsl`（单一包名） |
| 版本 | `26.3.0-main`（**只有一个版本**，不要 reintroduce release/debug/ubwc 变体） |
| FCL 显示名 | `des` 和 `renderer` 首段都是 `KGSL-Freedreno（OpenGL 4.6）`（全角括号） |
| 应用名 | `Mesa Freedreno A730 (26.3 UBWC)` |
| MC 版本 | 不限：`minMCVer`/`maxMCVer` 留空 |
| 图标 | 方案 B（芯片 + 穿透箭头），由 `plugin/icon/make-icon.py` 生成，勿手改 PNG |

**设置通道**：Activity 写 `Android/media/pkg.freedreno.kgsl/fcl_env.txt`，驱动在
`dri2_initialize_android()` 里由 `fcl_load_ab_config()` 读取并 `setenv`。
**不要**恢复 `/sdcard/FCL/fcl_ab.txt` 那条老通道。

`ubwc / notile / flush / sysmem / nobin / FCL_NO_FAST_CLEAR` 都是**运行时开关**，
通过插件设置界面写文件，不是打包变体。

## 构建与打包

```bash
scripts/build-egl.sh          # ninja → dist/libEGL_mesa_gl.so, dist/libgallium_dri.so
cd plugin && ./build.sh       # → dist/MesaFreedreno-26.3-main.apk
```

- 依赖：`plugin/android-29.jar`（Android SDK 拷来）、`tools/r8.jar`（d8，Google Maven
  `com.android.tools:r8`）。两者都**不入库**，缺失时脚本会报错。
- 签名：默认用仓库外的 `/home/lcys/文档/deepseek-harness/default-workspace/signing-certs`
  下的 `codesign/codesign.p12`（可用 `SIGNING_CERTS_DIR` 覆盖），口令从同目录
  `passwords.txt` 读。**不要打印口令**，也不要提交证书。
- 构建产物全部在 `dist/`（gitignore），因此构建不会产生 git 变更。
- 只有用户明确要求时才构建；不要为了让产物变新而顺手跑一遍。

## 改 Mesa 的规矩

1. 先读代码再改，改完把**整个文件**拷进 `mesa/patches/`；新增文件要同时加进 `mesa/patches/MAP`。
2. KGSL 后端是 **SolDev69/freedreno-gallium-mesa 24.1.2 原版**，只允许 3 处 26.x 适配
   （`dev->features`、`.reset_status`、suballoc IB 地址），见 `mesa/patches/kgsl/README.md`。
   **不要**再往 `kgsl_bo.c` 里加自研的 pending 释放队列 / 强制 IOCOHERENT / cache 维护，
   除非有新的实测证据。
3. 改 flush / CCU / UBWC 相关代码时，**a6xx 和 a7xx 两条分支都要看**：本机是 a7xx，
   但代码里同时有 `CHIP == A6XX` 分支，改错会互相影响。
4. 不要在 `libgallium_dri` 里留调试 `fprintf`（EGL 层历史遗留的 `[fcl-dri]` 可以留）。

## 上机调试

- 设备 adb 端口会变：先 `adb devices`，不要硬编码序列号。
- 装完插件、改完设置、换完库：**必须 `adb shell am force-stop com.tungsten.fcl`**，
  否则 FCL 用的是旧的 `nativeLibraryDir` / 进程内旧驱动。
- 自检：`tools/gltest`（`UBWC-*` 用例，logcat 标签 `GLTEST`）。
- A/B：`ADB="adb -s <serial>" tools/ab-mc.sh env "<KEY=VALUE...>"` 写配置并重启 FCL
  （脚本里有个写死的旧端口默认值，一定要用 `ADB=` 覆盖）；
  `tools/flicker.py` 算逐帧闪烁指标。
- 判据：截图后裁热栏
  `convert shot.png -crop 1300x140+880+1650 +repage -resize 300% hotbar.png`。
- 设备是 root（KernelSU），`/dev/kgsl-3d0` 可直接 open。

## 提交风格

- commit message 用中文，一个主题一个提交；打包时把 APK 的 sha256 写进去。
- 不提交二进制（`dist/`、`archive/` 已 gitignore）；不 push、不改写历史。
- 上游 Mesa 那套 AGENTS.md（不写注释、不加 AI trailer 等）只在**往上游提交**时适用；
  本仓库是下游 fork，不向上游提 MR。

## 已知坑（改代码前先确认没踩）

| 现象 | 原因 |
|---|---|
| `cannot get version` / screen 建不起来 | KGSL 没有 DRM 节点，`drmGetVersion()` 返回 NULL → 走 `kgsl_device_new()` 回退 |
| `create_screen: dd=kmsro` | 驱动名被解析成 kmsro；必须用 `driver_name = "freedreno"` 并覆盖 `LIBGL_DRIVERS_PATH` |
| 驱动加载失败 `libdrm.so not found` | 26.3 产物 NEEDED libdrm；要么静态链，要么一起打包 |
| `eglCreateContext(4.6 core)` 返回 0 / `0x3009` | 没先 `eglBindAPI(EGL_OPENGL_API)`，config 没带 `EGL_OPENGL_BIT` |
| 花屏只在 UBWC 下出现 | a7xx 的 blit cache 必须显式 clean（`FD_CCU_CLEAN_BLIT_CACHE`）；KGSL 没有隐式同步 |
