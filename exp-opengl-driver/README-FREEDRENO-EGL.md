# Mesa freedreno EGL（零拷贝直出）for Adreno 730 — FCL 渲染器插件

生成时间：2026-10-24 · 目标设备：SM8475 / Adreno 730 v3 / Android 15
Mesa 25.0.7 · freedreno KGSL 后端 · 桌面 GL 4.6 + GLSL 4.60

## 1. 产物

| 文件 | 说明 | sha256 |
|---|---|---|
| `release-25.0.7-a730-egl/MesaFreedrenoEGL-A730-25.0.7.apk` | FCL 渲染器插件，versionCode 3 | `e89ed851…46229` |
| `release-25.0.7-a730-egl/libEGL_mesa_gl.so` | 自编 libEGL（droid 平台 + KGSL 钩子），SONAME `libEGL_mesa_gl.so` | `120ffb90…7cfb` |
| `release-25.0.7-a730-egl/libgallium_dri.so` | freedreno Gallium 驱动 | `07bd302b…d8ca` |

APK 内另含 `libEGL_freedreno.so`：插件入口 shim，导出**完整 EGL ABI**
（55 个入口，含 `eglGetDisplay` / `eglCreatePlatformWindowSurface` 等）以及
`glXGetProcAddress[ARB]`，全部转发给真正的 libEGL。

完整 sha256 见 `release-25.0.7-a730-egl/SHA256SUMS`。

## 2. 它是什么

FCL 的 gl_bridge（`POJAV_RENDERER=opengles*`）用 EGL 创建窗口面。
本插件把 EGL 换成 Mesa 25.0.7 + freedreno/KGSL，直接渲染到 FCL 传进来的
`ANativeWindow` 的 gralloc buffer（dmabuf），**零拷贝上屏**，不经过 OSMesa
的每帧 `ANativeWindow_lock` + 全帧回读。

实测（MC 1.12.2，2880×1800）：

| 渲染器 | 帧率 |
|---|---|
| OSMesa（回读） | 14 fps |
| Zink（Vulkan WSI） | 29 fps |
| **本插件（EGL 直出）** | **~39 fps** |

## 3. 构建

```bash
# 1) Mesa（EGL/GLES/GL + freedreno/KGSL），产物进 release-25.0.7-a730-egl/
./build-freedreno-egl.sh

# 2) 打包插件（shim + res 图标 + 签名）
cd fcl-plugin && ./build-egl-plugin.sh
```

`build-freedreno-egl.sh` 会把 `patches/egl/` 里的整文件补丁覆盖进源码树。
图标由 `fcl-plugin/icon/make-icon.py` 生成（PIL，5 档密度）。

## 4. 安装

```bash
adb uninstall com.a730.mesa.egl          # 旧 debug 签名不能覆盖升级
adb install fcl-plugin/MesaFreedrenoEGL-A730-25.0.7.apk
```

FCL → 版本设置 → 渲染器 → **Freedreno-EGL** → 启动。

插件签名使用个人代码签名（CN=lincinyshan）；证书目录可用 `SIGNING_CERTS_DIR`
覆盖，口令取 `KEYSTORE_PASSWORD` 或 `<certs>/passwords.txt`，缺失时回落
debug keystore。

## 5. Minecraft 版本支持

- **26.2 及以下**：正常。
- **26.3**：也走 OpenGL。MC 26.3 的渲染后端会先尝试 OpenGL，失败时**静默回退
  Vulkan**（日志表现为 `Using graphics backend Vulkan`）。早期插件在 26.3 上失败
  的原因不是“26.3 强制 Vulkan”，而是 shim 当时只导出了 `eglGetProcAddress`；
  26.3 / LWJGL 3.4.1 / SDL 会直接 `dlsym` `eglGetDisplay` 等常规入口，于是报
  `Could not retrieve EGL function eglGetDisplay` 后回退 Vulkan。现在 shim 导出
  完整 EGL ABI（55 个入口），26.3 可以正常选 OpenGL。

## 6. 关键技术点（都在 patches/egl/ 里）

1. **`native_window_api_connect(NATIVE_WINDOW_API_EGL)`**
   厂商 libEGL 会替应用做，Mesa 的 droid 路径不做。缺少时
   `ANativeWindow_dequeueBuffer()` 永远返回 `-ENODEV(NO_INIT)`，EGLSurface
   拿不到 buffer（默认帧缓冲 0x0、每帧 `GL_INVALID_OPERATION`）。
   实现：用 `ANativeWindow` 的 `perform` 指针（偏移 0x98，由系统
   `libnativewindow.so` 的 `ANativeWindow_lock` thunk 反汇编得到）调
   op 13 / op 14。
2. **`screen->dmabuf_import` / `has_dmabuf`**
   KGSL 无 DRM 节点/DRI3，`dri_init_screen()` 默认关闭，导入窗口 dmabuf 时
   直接 `__DRI_IMAGE_ERROR_BAD_PARAMETER`。KGSL 钩子里创建屏幕后强制置位。
3. **swap 的 out fence**
   Mesa 只在通用（X11/Wayland）路径生成 fence，Android 的
   `droid_swap_buffers()` 不生成，`queueBuffer` 不带 fence → 消费端可能取到
   半渲染的帧（撕裂）。在 flush 后调用 `dri2_surf_update_fence_fd()`。
4. **无 DRM 节点的屏幕创建**：直接 `open("/dev/kgsl-3d0")`，
   `dri2_load_driver` + `dri2_create_screen`，并让 pipe_loader 回退到
   `"kgsl"` 描述符。
5. **Android 上开放桌面 GL**：`eglcurrent.h` 去掉 `!DETECT_OS_ANDROID` 限制。
6. **插件 shim 导出完整 EGL ABI**（`tools/egl_bridge_shim.c`）：除
   `eglGetProcAddress` / `glXGetProcAddress[ARB]` 外，还导出 55 个 EGL 1.0–1.5 核心
   与常用 EXT/KHR 入口（`eglGetDisplay`、`eglCreatePlatformWindowSurface`、
   `eglSwapBuffersWithDamageKHR` 等），全部转发给真正的 libEGL。新版本游戏
   （MC 26.3 / LWJGL 3.4.1 / SDL）会直接 `dlsym` 这些符号，缺一个就会判定
   “OpenGL is not supported”并回退 Vulkan。

调试时可参考 `tools/gltest/`：纯 native 的 NativeActivity 复现 APK
（不需要 Java/dex），把 stderr 重定向到 App 私有目录，用
`adb shell run-as com.a730.egltest cat files/gltest.log` 查看。

## 7. 已知限制

- 每帧都会重新 import 一次窗口 dmabuf（Mesa droid 的设计），可缓存优化；
- 启动时仍会打印少量 `[fcl-egl]` 诊断行（一次性，不影响性能）；
- 深度/模板附件为驱动自行分配，窗口 buffer 只承载颜色。
