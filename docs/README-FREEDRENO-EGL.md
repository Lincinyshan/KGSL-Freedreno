# Mesa freedreno EGL 直出 —— 实现说明

> 版本：**v1.0.2** · Mesa **26.3.0-rc3** · 2026-11-01
> 设备：SM8475 / Adreno 730v3 / Android 15 / KGSL

> [!WARNING]
> 本文结论**随时可能被推翻**：它们只是写这份文档时最强的证据。后续若出现新日志或新代码依据，以**实测与源码**为准，并直接更新文档（提交信息里写清依据）。

本文说明这个插件的**实现原理**。使用与构建见 [`README.md`](../README.md)，
踩坑与修复见 [`HANDOVER.md`](HANDOVER.md)。

## 1. 产物

| 文件 | 说明 |
|---|---|
| `dist/MesaFreedreno-26.3-main.apk` | FCL 插件（package `pkg.freedreno.kgsl`，1.0.2 / versionCode 3） |
| `dist/libEGL_mesa_gl.so` | 自编 Mesa libEGL（Android droid 平台 + KGSL 钩子） |
| `dist/libgallium_dri.so` | freedreno gallium 驱动（含 KGSL 后端） |
| APK 内 `libEGL_freedreno.so` | 插件入口 shim：导出完整 EGL ABI（62 个入口）并转发 |

## 2. 数据流

```text
Minecraft (LWJGL / SDL)
  │ eglGetDisplay / eglCreateContext / eglSwapBuffers ...
  ▼
libEGL_freedreno.so          插件 shim（tools/egl_bridge_shim.c）
  │  - 预加载 libdrm.so / libgallium_dri.so（满足 linker namespace）
  │  - 强制桌面 GL、覆盖 profile / ES 属性
  ▼
libEGL_mesa_gl.so            Mesa libEGL（droid 平台）
  │  - drmGetVersion() 失败 → open /dev/kgsl-3d0
  │  - 读 fcl_env.txt 并 setenv
  ▼
libgallium_dri.so            freedreno + KGSL
  │  ioctl(KGSL_GPU_COMMAND)
  ▼
/dev/kgsl-3d0
```

渲染直接进 `ANativeWindow` 的 gralloc buffer（dmabuf），**零拷贝上屏**，
不经过 OSMesa 的每帧 `ANativeWindow_lock` + 全帧回读。

## 3. 关键技术点（都在 `mesa/patches/` 与 `tools/egl_bridge_shim.c`）

1. **`native_window_api_connect(NATIVE_WINDOW_API_EGL)`**
   厂商 libEGL 会替应用做，Mesa droid 路径不做；缺少时
   `ANativeWindow_dequeueBuffer()` 永远 `-ENODEV`（默认帧缓冲 0x0）。
2. **`screen->dmabuf_import` / `has_dmabuf`**
   KGSL 无 DRM 节点/DRI3，`dri_init_screen()` 默认关闭；KGSL 钩子创建屏幕后强制置位。
3. **swap 的 out fence**
   Mesa 只在 X11/Wayland 路径生成 fence，Android 的 `droid_swap_buffers()` 不生成 →
   `queueBuffer` 不带 fence（撕裂）。在 flush 后调 `dri2_surf_update_fence_fd()`。
4. **无 DRM 节点的屏幕创建**
   `drmGetVersion()` 失败时 `kgsl_device_new(fd)`；pipe_loader 回退到 `"kgsl"`。
5. **Android 上开放桌面 GL**：`eglcurrent.h` 去掉 `!DETECT_OS_ANDROID` 限制。
6. **强制桌面 GL**：MC 26.3 用 SDL 建窗口，SDL 在 Android 上一律请求 GLES；shim 接管：
   `eglBindAPI` 一律绑桌面 GL，`eglCreateContext` 丢掉 ES client-version 直接要 4.6，
   `eglChooseConfig` 兜底换 `EGL_OPENGL_BIT`。
7. **GL profile 可选**：`FCL_GL_FORCE_CORE` / `FCL_GL_FORCE_COMPAT`（设置界面二选一）。
   默认透传游戏请求；Iris 的部分光影包写死 `#version 150 compatibility`，需要 Compatibility。
8. **shim 导出完整 ABI**（62 个入口，构建时校验）：新版本游戏（MC 26.3 / LWJGL 3.4.1 /
   SDL）会直接 `dlsym` 常规 EGL 入口，缺一个就判定 “OpenGL is not supported” 并回退 Vulkan。

## 4. 安装 / 使用

```bash
adb install -r dist/MesaFreedreno-26.3-main.apk
adb shell am force-stop com.tungsten.fcl        # 必须，见下
```

FCL → 版本设置 → 渲染器 → **KGSL-Freedreno（OpenGL 4.6）** → 启动。

> **覆盖安装后必须 force-stop FCL**：FCL 把插件的 `nativeLibraryDir` 缓存在进程里，
> 覆盖安装会换掉 `/data/app/~~<hash>` 目录；不重启会报
> `Failed to dynamically load library: .../libEGL_freedreno.so (error = null)`
> —— `error = null` 是假象（FCL 自己的加载器先调过 `dlerror()`），真实原因是旧路径已不存在。

## 5. Minecraft 版本支持

- **26.3**：正常（已验证，Core / Compatibility 均可）。
- **更早版本**：FCL 自带 gl_bridge 会 `eglBindAPI(EGL_OPENGL_API)`，插件同样可用。
- 26.3 的 OpenGL 后端失败时会**静默回退 Vulkan**；日志里出现
  `Using graphics backend Vulkan` 基本就是 EGL 初始化失败，先看插件是否安装/是否 force-stop。

## 6. 调试

- 插件设置界面写 `Android/media/pkg.freedreno.kgsl/fcl_env.txt`；改完 force-stop FCL；
- `tools/gltest`：纯 native NativeActivity 复现 APK，会把 stderr 重定向到 App 私有目录
  （`adb shell run-as com.a730.egltest cat files/gltest.log`）；
- 驱动侧的 `[fcl-egl]` / `[fcl-dri]` 打印写 stderr，FCL 不接管，需要时用 gltest 看。

## 7. 已知限制

- 每帧重新 import 一次窗口 dmabuf（Mesa droid 的设计），可缓存优化；
- 启动时打印少量 `[fcl-egl]` 诊断行（一次性）；
- 深度/模板附件由驱动自行分配，窗口 buffer 只承载颜色；
- 只带 `arm64-v8a`，只编了 KGSL 后端。
