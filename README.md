# gpu-driver — Mesa freedreno (KGSL) for FCL

> [!NOTE]
> The project was built entirely with vibecoding.

给 FCL（Minecraft Java 版启动器）用的 **OpenGL 驱动插件**：Mesa 26.3 的 EGL +
freedreno gallium 驱动 + KGSL 后端，ANativeWindow 直出（零拷贝）。

> 注：简单来说：__纯原生__

- 目标设备：Adreno 730v3（chip_id `0x07030002`，a7xx_gen1，KGSL）//注：理论上整个7系Adreno都能用
- 插件包名：`pkg.freedreno.kgsl`，版本 `26.3.0-main`（单一版本，无 release/debug 之分）
- FCL 里显示名：`KGSL-Freedreno（OpenGL 4.6）`

> 非官方项目，与 Mesa、Qualcomm、FCL 均无隶属关系。

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
| `build/` | Meson 构建目录（不入库，可由 `BUILD_DIR` 改到别处） |
| `.env.local` | 本机路径覆盖（不入库，见下） |

## 构建 / 打包

依赖：Mesa 26.3.0-rc1 源码、Android NDK r30（API 35）、meson/ninja、Android SDK build-tools。
静态 `libdrm` 可选（`DEPS_DIR`，其 `lib/pkgconfig` 下要有 `libdrm.pc`）。

```bash
# 1) 配置 Mesa 构建目录（只需一次）
export MESA_SRC=/path/to/mesa-26.3.0-rc1
export NDK_DIR=/path/to/android-ndk-r30
export DEPS_DIR=/path/to/static-deps     # 可选：静态 libdrm
scripts/setup-build.sh

# 2) 编 Mesa 产物
scripts/build-egl.sh                     # → dist/libEGL_mesa_gl.so, dist/libgallium_dri.so

# 3) 打 FCL 插件（依赖 dist/ 里的两个 .so）
cd plugin && ./build.sh                  # → dist/MesaFreedreno-26.3-main.apk
```

本机路径可以写进仓库根的 `.env.local`（`BUILD_DIR=` / `CCACHE_DIR=` …，不入库），
脚本会先 source 它；显式传入的环境变量优先。

插件构建需要 `plugin/android-29.jar`（Android SDK 里拷）和 `tools/r8.jar`（d8，Google Maven
`com.android.tools:r8`），两者都不入库。

**签名**：官方发布包由维护者的私钥签名（证书 SHA-256 指纹
`66ab6f193ed3e6d6637d66e0b004a38505b00056f0b7b127da5f1422563362b8`，可自行核对）。
仓库不包含证书与口令，构建时用 `SIGNING_CERTS_DIR` 指向的 `codesign/codesign.p12`；
没有则回退 `plugin/debug.keystore` 调试签名——**调试签名的包覆盖不了官方版**，需要先卸载。

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

## 来源与许可

- **Mesa 3D**（MIT）— freedreno 驱动、EGL 平台层、`src/freedreno/fdl` 等，
  `mesa/patches/` 里的文件都保留了原始版权头。
- **[SolDev69/freedreno-gallium-mesa](https://github.com/SolDev69/freedreno-gallium-mesa)**（MIT）
  — KGSL gallium 后端（`mesa/patches/kgsl/`）。
- 本仓库新增的脚本与插件代码 — MIT，见 [LICENSE](LICENSE)。

不包含 Qualcomm 专有代码，也未使用 Qualcomm 商标或图形素材。
