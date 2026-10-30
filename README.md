# gpu-driver — Mesa freedreno (KGSL) for FCL

给 FCL（Minecraft Java 版启动器）用的 **OpenGL 驱动插件**：Mesa 26.3 的 EGL +
freedreno gallium 驱动 + KGSL 后端，ANativeWindow 直出（零拷贝）。

- 目标设备：Adreno 730v3（chip_id `0x07030002`，a7xx_gen1，KGSL）
- 插件包名：`pkg.freedreno.kgsl`，版本 `26.3.0-main`（单一版本，无 release/debug 之分）
- FCL 里显示名：`KGSL-Freedreno（OpenGL 4.6）`

## 目录

| 路径 | 内容 |
|---|---|
| `plugin/` | FCL 插件（唯一一份）：`AndroidManifest.xml`、`java/pkg/freedreno/kgsl/MainActivity.java`（调试开关界面）、`res/` 图标、`build.sh` |
| `mesa/patches/` | **改过的上游文件快照**（不是 diff）：EGL 平台层、gallium freedreno、`kgsl/` 后端；`MAP` 是文件→路径对照 |
| `scripts/build-egl.sh` | 编 Mesa 产物到 `dist/`（ninja 复用已有构建目录） |
| `tools/` | `gltest`（UBWC 自检）、`ab-mc.sh`/`flicker.py`（A/B 与闪烁度量）、`egl_bridge_shim.c`（EGL shim） |
| `docs/` | `HANDOVER.md`、`NOTES-ubwc-artifacts.md`、`NOTES-turnip-vs-gallium-ubwc.md`、`README-FREEDRENO-EGL.md` |
| `dist/` | 产物（不入库）：`libEGL_mesa_gl.so`、`libgallium_dri.so`、`*.apk` |
| `archive/` | 旧版本/旧清单/日志（不入库） |
| `exp-opengl-driver/gl_workdir/` | **Meson 构建工作区，路径固定别改名**（build-egl-26.3、deps-static、工具链 cross file） |
| `vulkan-driver/` | Turnip 构建器 + Mesa 26.3 源码树（上游代码，不入库） |

## 构建 / 打包

```bash
# 1) Mesa 产物（需要 gl_workdir/build-egl-26.3 已由 meson 配好）
scripts/build-egl.sh                      # → dist/libEGL_mesa_gl.so, dist/libgallium_dri.so

# 2) FCL 插件（依赖 dist/ 里的两个 .so）
cd plugin && ./build.sh                   # → dist/MesaFreedreno-26.3-main.apk
```

插件构建需要 `plugin/android-29.jar`（Android SDK 里拷）和 `tools/r8.jar`（d8，Google Maven
`com.android.tools:r8`），两者都不入库。

## 调试开关

桌面点插件图标进入设置界面，勾选后写
`/storage/emulated/0/Android/media/pkg.freedreno.kgsl/fcl_env.txt`，
驱动在 EGL 初始化时（`fcl_load_ab_config()`）读它并 `setenv`。**改完必须 force-stop FCL**。

| 开关 | 作用 |
|---|---|
| `flush` / `notile` / `noubwc` / `sysmem` / `nobin` | `FD_MESA_DEBUG` 的调试项 |
| `FCL_NO_FAST_CLEAR` | 强制关闭 a7xx GMEM fast clear |

## 关键实现点

- Mesa 26.3 上游删掉了 gallium 的 KGSL 后端，这里用
  [SolDev69/freedreno-gallium-mesa](https://github.com/SolDev69/freedreno-gallium-mesa) 的
  24.1.2 版本（相对原版只保留 3 处 26.x 适配，见 `mesa/patches/kgsl/README.md`）。
- EGL 平台层在 `drmGetVersion()` 失败（KGSL 没有 DRM 节点）时直接 open `/dev/kgsl-3d0`，
  并把 `LIBGL_DRIVERS_PATH` 指到插件目录。
- 直出：`libEGL_freedreno.so` 是 shim，转发 EGL 符号到我们的 `libEGL_mesa_gl.so`。

## 已知问题

见 `docs/NOTES-ubwc-artifacts.md`（UBWC 花屏排查）与
`docs/NOTES-turnip-vs-gallium-ubwc.md`（与 Turnip 的实现差异）。
