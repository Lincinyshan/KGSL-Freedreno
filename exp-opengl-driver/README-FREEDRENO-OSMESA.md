# Mesa freedreno OSMesa (OpenGL 4.6) for Adreno 730 — FCL 渲染器插件

生成时间：2026-10-23 · 目标设备：SM8475 / Adreno 730 v3（chip_id `0x07030002`）/ Android 15

## 1. 产物

| 文件 | 说明 | sha256 |
|---|---|---|
| `release-25.0.7-a730/MesaFreedreno-A730-25.0.7.apk` | FCL 渲染器插件（含 `libOSMesa_8.so`） | `c13d8c13…dfc6` |
| `release-25.0.7-a730/libOSMesa_8.so` | Mesa 25.0.7 OSMesa + freedreno(KGSL)，可单独使用 | `7ae6a1eb…7a23` |

md5 见 `release-25.0.7-a730/SHA256SUMS`。

## 2. 它是什么

FCL（FoldCraftLauncher）内置的 “Freedreno” 渲染器走的是
`POJAV_RENDERER=gallium_freedreno` → OSMesa 创建桌面 GL 上下文 → 渲染到
`ANativeWindow` 的 CPU buffer。FCL 自带的 `libOSMesa_8.so` 是 **Mesa 23.0.4**
（NDK clang 14 编译），本产物把它换成 **Mesa 25.0.7**：

- freedreno 对 a6xx+（a730 = gen 7）设 `glsl_feature_level = 460`，
  因此驱动原生上报 **OpenGL 4.6 / GLSL 4.60**（不再依赖写死的 override）；
- 打上 `FD725 → Adreno (TM) 730` 显示名补丁（只改 name，不动 chip 数据/调优）；
- 使用 KGSL 后端（`/dev/kgsl-3d0`），`-march=armv8.2-a+dotprod+fp16`；
- 静态链接 libdrm，运行期只依赖 `liblog / libz / libm / libdl / libc`（都是公开系统库）。

### 为什么是 Mesa 25.0.7

OSMesa 在 **25.1.0 被上游删除**；25.0.x 是最后一个带 OSMesa 的版本。
freedreno 的 GL 4.6（GLSL 460）在 25.0 已经存在，a730v3（`0x07030002`）
自 24.3 起被识别，因此 25.0.7 是“既有 OSMesa 又有 GL 4.6 + a730v3”的最高版本。

## 3. 相对上游的补丁（`build-freedreno-osmesa.sh` 内幂等应用）

1. `src/freedreno/common/freedreno_devices.py`：`name="FD725"` → `"Adreno (TM) 730"`。
2. `src/gallium/targets/osmesa/osmesa_target.c`：当 `GALLIUM_DRIVER=freedreno`
   时 `open("/dev/kgsl-3d0")` 并 `fd_screen_create(fd, NULL, NULL)`，
   不再创建 swrast screen（FCL 的 `egl_bridge.c` 对 `gallium_freedreno`
   正好设置这个环境变量）。
3. `src/gallium/targets/osmesa/meson.build`：把 `driver_freedreno`、`dep_libdrm`
   链进 `libOSMesa`。
4. `src/util/perf/cpu_trace.h`：关掉 atrace 分支（去掉 `libcutils` 依赖）。
5. `src/util/os_misc.c`：`property_get` → `__system_property_get`（libc），
   并补 `PROPERTY_VALUE_MAX/PROPERTY_KEY_MAX` 宏。
6. `src/util/libsync.h`：Android 上改回 ioctl fallback（去掉 `libsync.so` 依赖）。
7. **KGSL Gallium 后端回移**：25.0.7 上游的 Gallium 没有 KGSL 后端
   （`-Dfreedreno-kmds=kgsl` 只对 Turnip 有效，`freedreno_device.c` 里
   `#if HAVE_FREEDRENO_KGSL` 是空钩子）。从
   `SolDev69/freedreno-gallium-mesa` 分支 `feat/mesa-update/24.1`
   回移 `src/freedreno/drm/kgsl/` 五个文件，并按 25.0.7 API 改掉
   `dev->features` 和 `fd_pipe_funcs.reset_status`；meson 里定义
   `HAVE_FREEDRENO_KGSL=1`。详见 `patches/kgsl/README.md`。

## 4. 复现构建

```bash
cd /home/lcys/android/gpu-driver/exp-opengl-driver
./build-freedreno-osmesa.sh                 # 增量：复用 gl_workdir 里的源码/构建目录
rm -rf gl_workdir/build-osmesa-25.0.7 && ./build-freedreno-osmesa.sh   # 全新构建（源码/依赖保留）
```

依赖（均已就位）：

- NDK r30：`gl_workdir/android-ndk-r30`（软链到 Turnip 那份）
- 静态 libdrm 2.4.131：`gl_workdir/deps-static`
- meson/ninja/ccache：`../vulkan-driver/.build-tools`
- Mesa 源码：`gl_workdir/mesa-25.0.7`（tarball `gl_workdir/mesa-25.0.7.tar.xz`）

构建目录：`gl_workdir/build-osmesa-25.0.7`。

## 5. 插件安装与使用

1. 安装 `MesaFreedreno-A730-25.0.7.apk`（普通 APK，需允许“未知来源”）。
2. 打开 FCL → 版本设置 → 渲染器，列表里会出现 **Freedreno-A730**。
3. 选择它启动游戏。

插件声明的环境变量（`AndroidManifest.xml`）：

```
POJAV_RENDERER=gallium_freedreno
LIB_MESA_NAME=libOSMesa_8.so          # FCL 会拼成插件 nativeLibraryDir 绝对路径
MESA_GL_VERSION_OVERRIDE=4.6
MESA_GLSL_VERSION_OVERRIDE=460
allow_higher_compat_version=true
force_glsl_extensions_warn=true
```

`renderer=Freedreno-A730:libOSMesa_8.so:libEGL.so`：GL 库用插件里的
`libOSMesa_8.so`，EGL 用系统 `libEGL.so`（与 FCL 内置 Freedreno 渲染器一致）。

## 6. 已验证 / 未验证

已验证（静态）：

- AArch64 ELF、SONAME `libOSMesa_8.so`；
- NEEDED 仅 `liblog, libz, libm, libdl, libc`；
- `strings` 有 `/dev/kgsl-3d0`、`freedreno`、`Adreno (TM) 730`；
- 导出 `OSMesaCreateContext/OSMesaMakeCurrent/OSMesaGetProcAddress` 等；
- 反汇编 `osmesa_create_screen` 确实 `open("/dev/kgsl-3d0")` + `fd_screen_create`；
- 未 strip 的构建产物里存在 `kgsl_device_new` / `kgsl_bo_new` / `kgsl_pipe_new`
  （KGSL 后端真的链进去了，不是只有一个 open 调用）；
- 二进制中存在 GLSL 460 常量（`mov w?, #0x1cc`），配合 25.0.7 源码
  `freedreno_screen.c` 的 `is_a6xx ? 460 : ...` 确认 4.6 路径；
- APK 已签名，`aapt dump xmltree` 元数据正确，`unzip` 内 `.so` 与发布文件 sha256 一致。

未验证（没有设备）：

- 真机能否 dlopen、OSMesa 上下文创建、`glGetString(GL_VERSION)` 是否 4.6；
- 与 FCL 当前版本的插件扫描/加载是否匹配（v1 插件协议，FCL 需支持渲染器插件）；
- KGSL 后端是回移代码，未在真机上跑过（FCL 自带那份是 Mesa 23.0.4 + 同类补丁）；
- 帧率/稳定性/KGSL 兼容性。

## 7. 排障

- `adb logcat | grep -E "GLBridge|OSMesa|libOSMesa"` 看插件是否被加载；
- 若渲染器列表里没有条目：确认 APK 已安装、FCL 版本支持“渲染器插件化”、
  插件管理页里没有被禁用；
- 若上下文创建失败：确认 `/dev/kgsl-3d0` 可打开（普通 App 可访问 GPU 设备，
  SELinux 由 `gpu_device` 允许）；
- 需要回退：在 FCL 里切回内置 “Freedreno” 渲染器，或卸载插件。

## 8. 附带的另一份构建（非 FCL 用）

`gl_workdir/build-mesa-gl` 是 **Mesa 26.3.0-rc1** 的 Android EGL/GLES 构建
（`libEGL.so`、`libGLESv2.so`、`libgallium_dri.so`），用于后续
“bionic 版 libGL / 自定义加载器”路线，不参与本插件。
