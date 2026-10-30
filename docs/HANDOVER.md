# 交接文档 — Turnip / freedreno-OSMesa / FCL（2026-10-23）

> 本文件是新的**唯一入口**，取代 `vulkan-driver/HANDOVER.md`（旧文档只描述迁移前的
> Turnip 构建，保留作历史）。
> 下一会话请先读本文件，再按 §0 的 **PTC** 方式工作。

---

## 0. 工作方式：PTC（Programmatic Tool Calling）

下一个会话应使用 **PTC 预设**：Agent 通过 `run_code` 写 TypeScript 程序，用生成的
工具 SDK 发起调用，而不是一次一个工具调用。程序里可以组织循环、条件判断、错误处理
和并发调用。

**本文件写于普通模式**（当前会话工具集里没有 `run_code`），但下面的步骤都可以
1:1 映射成 PTC 程序：

| 适合用程序编排 | 说明 |
|---|---|
| 批量校验 | 循环 `md5sum`/`sha256sum` 所有 release 产物，比对 §3 表格 |
| 循环打补丁 | 对 Mesa 源文件逐项 grep + sed/python 替换，失败即抛错 |
| 日志过滤 | 构建后只取 `error:`/`FAILED`/最后 40 行，避免把整份日志塞进上下文 |
| 顺序构建 | **编译必须串行**（2 核 / 3.7 GiB 内存），用程序做“失败→tail 日志→重试” |
| 只读并发 | `strings`/`readelf`/`grep` 这类互不影响的检查可以并发 |

**不要并发跑两个重型编译**（Mesa 全量 + Turnip 全量会互相拖垮内存/swap）。

---

## 1. 环境现状

| 项 | 值 |
|---|---|
| 工作区 | `/home/lcys/android/gpu-driver` |
| Turnip | `vulkan-driver/Mesa-Turnip-Builder`（git 分支 `stable`，3 处未提交改动，**勿 `git checkout -- .`**） |
| GL 实验 | `exp-opengl-driver`（含构建脚本、补丁、release、FCL 插件） |
| NDK | `/home/lcys/android/android-ndk-r30`（r30 / `30.0.16248370`）；`gpu-driver/android-ndk-r30` 是软链 |
| SDK | `/usr/lib/android-sdk`（只有 build-tools 29.0.3，**无 platform/android.jar**） |
| 工具链 | `vulkan-driver/.build-tools`（meson 1.12.1、ninja 1.13.2、ccache 4.14.1、patchelf、glslang） |
| ccache | `vulkan-driver/Mesa-Turnip-Builder/.ccache`（6 GB 上限，两个项目共用） |
| 磁盘 | 35 GB 已用 / 74 GB 可用 |
| 内存 | 3.7 GiB + 8 GiB swap，`JOBS=2` |

### 1.1 NDK 修复（重要）

`turnip_workdir` 里那份 NDK 副本已删除，统一用 `/home/lcys/android/android-ndk-r30`。
但那份 NDK 有**符号链接被复制成文本文件**的损坏，已修复：

- `ld.lld/ld64.lld/wasm-ld/ld/lld-link` → `lld`
- `clang++` → `clang`
- `llvm-strip` → `llvm-objcopy`；`llvm-ranlib/llvm-lib/llvm-dlltool` → `llvm-ar`
- `llvm-readelf` → `llvm-readobj`；`llvm-addr2line` → `llvm-symbolizer`
- `llvm-windres` → `llvm-rc`；`perf2bolt` → `llvm-bolt`

验证：`ld.lld --version` = LLD 21.0.0；`clang++ --version` = clang 21.0.0；
aarch64 目标与宿主 C++ 链接都通过。

还剩 24 个同样形态的占位文件，全部在 **python3 / lldb / host 库**（如
`libc++.so.1`），不参与交叉编译，未处理。

### 1.2 构建目录状态

| 目录 | 状态 |
|---|---|
| `turnip_workdir/mesa-mesa-26.3.0-rc1/build-android-aarch64` | **已用新 NDK 重新 configure，未编译**（旧目录含迁移前绝对路径，已删） |
| `exp-opengl-driver/gl_workdir/build-osmesa-25.0.7` | 完整，`ninja -n` 无事可做 |
| `exp-opengl-driver/gl_workdir/build-mesa-gl`（26.3 EGL） | 完整 |

### 1.3 脚本改动（`build-turnip.sh`）

1. **NDK 自动复用**：`NDK_DIR` > `$script_dir/../../android-ndk-r30` >
   `$HOME/android/android-ndk-r30` > 下载；不再往 workdir 解压第二份。
2. **stale build dir 自愈**：`meson --reconfigure` 因旧绝对路径失败时，自动删
   构建目录并重新 setup（原来会直接退出）。

旧的 `README.md` / `.gitignore` 改动仍在（Turnip 定制），不要回滚。

---

## 2. 目标设备

- SoC **SM8475**（骁龙 8+ Gen 1），GPU **Adreno 730 v3**，chip_id `0x07030002`
- Android 15（API 35），KGSL（`freedreno-kmds=kgsl`）
- 应用目标：**FCL（FoldCraftLauncher）**，Minecraft Java Edition
- 最终诉求：**桌面 OpenGL 4.6**

---

## 3. 已完成的工作

### 3.1 Turnip（已真机验证）

产物在 `vulkan-driver/Mesa-Turnip-Builder/release-26.3.0-rc1-a730/`：

| 文件 | md5 |
|---|---|
| `Turnip-26.3.0-rc1-A730-MAGISK-KSU.zip` | `c4e6a59f788ec70f3243eb693d540f6a` |
| `Turnip-26.3.0-rc1-A730-EMULATOR.zip` | `d19597bf44be7760b25b688c70ba6107` |
| `vulkan.turnip.so` | `fbfd0c0cd479d484a8f7e506403f074b` |

用户实测**没问题**。重建命令（NDK 已改，脚本会自动探测）：

```bash
cd vulkan-driver/Mesa-Turnip-Builder
DEVICE_PROFILE=a730 MESA_VER=26.3.0-rc1 LTO_MODE=off BUILD_TAG=-A730 bash build-turnip.sh
```

> 注意：build dir 已删，首次会全量编译（ccache 部分命中），约 15–25 分钟。

### 3.2 Mesa 25.0.7 OSMesa + freedreno/KGSL（FCL 渲染器插件，待真机验证）

**为什么是 25.0.7**：OSMesa 在 Mesa 25.1 被上游删除；25.0.7 是最后一个带 OSMesa
的版本，且 freedreno 对 a6xx+（a730 = gen 7）已经报 `glsl_feature_level = 460`
（原生 GL 4.6 / GLSL 4.60），a730v3 也已识别。

**产物** `exp-opengl-driver/release-25.0.7-a730/`：

| 文件 | sha256 |
|---|---|
| `MesaFreedreno-A730-25.0.7.apk` | `c13d8c13f7c9d65d250f1df806ce2c0d0bce4948555d826f671e30041506dfc6` |
| `libOSMesa_8.so` | `7ae6a1eb8c250223e2363615a49be07c6ab8f0a605db15359ff6d5da1d027a23` |

md5：APK `87fdf54ccd1b1cae28b8394e360267ef`，so `fd344b623d74f3cc776115e5585084d5`。

**补丁**（`exp-opengl-driver/build-freedreno-osmesa.sh` 内幂等应用）：

1. `freedreno_devices.py`：`FD725` → `Adreno (TM) 730`（只改显示名）
2. `osmesa_target.c`：`GALLIUM_DRIVER=freedreno` 时 `open("/dev/kgsl-3d0")` +
   `fd_screen_create(fd)`（FCL 的 `egl_bridge.c` 正好设这个变量）
3. `osmesa` meson：链入 `driver_freedreno` + `dep_libdrm`
4. `cpu_trace.h`：关 atrace（去掉 libcutils）
5. `os_misc.c`：`property_get` → `__system_property_get`（libc）
6. `libsync.h`：Android 上改回 ioctl fallback（去掉 libsync.so）
7. **KGSL Gallium 后端回移**：上游 25.0.7 的 Gallium 没有 KGSL（只有 Turnip 有），
   从 `SolDev69/freedreno-gallium-mesa` 分支 `feat/mesa-update/24.1` 回移
   `src/freedreno/drm/kgsl/` 5 个文件，去掉 25.0.7 已不存在的
   `dev->features` 与 `fd_pipe_funcs.reset_status`。详见 `patches/kgsl/README.md`。

**验证（静态）**：AArch64、SONAME `libOSMesa_8.so`、NEEDED 仅
`liblog/libz/libm/libdl/libc`、`/dev/kgsl-3d0`、`Adreno (TM) 730`、
`kgsl_device_new/kgsl_bo_new/kgsl_pipe_new` 已链接、`fd_device_new` 反汇编里有
`strcmp("kgsl")` 分支、二进制里有 GLSL 460 常量。

**FCL 插件**：v1 协议（`fclPlugin` + `renderer` + `pojavEnv`），
`renderer=Freedreno-A730:libOSMesa_8.so:libEGL.so`，
`POJAV_RENDERER=gallium_freedreno`、`LIB_MESA_NAME=libOSMesa_8.so`、
`MESA_GL_VERSION_OVERRIDE=4.6`。安装后 FCL 渲染器列表出现 **Freedreno-A730**。

**未验证**：真机 dlopen、OSMesa 上下文、`GL_VERSION` 是否 4.6、FCL 版本对 v1 插件
的支持、KGSL 回移的实际行为。**这是当前唯一待你做的测试。**

### 3.3 Mesa 26.3 EGL/GLES（未打包，桥路线备用）

`exp-opengl-driver/gl_workdir/build-mesa-gl/`：

- `src/egl/libEGL.so`（330 KB）
- `src/mesa/glapi/es2api/libGLESv2.so`（87 KB）
- `src/gallium/targets/dri/libgallium_dri.so`（22.8 MB）

条件：`-Dplatforms=android -Dandroid-stub=true -Dgallium-drivers=freedreno
-Dfreedreno-kmds=kgsl -Degl=enabled -Dgles2=enabled`，API 35。
NEEDED 里包含 `libgallium_dri.so`、`libdrm.so`、`libhardware/libnativewindow/libsync`
（stub 名，运行时解析到系统库）。

**缺口**：26.3 的 Gallium 没有 KGSL 后端；EGL/DRI 在 Android 上没有打开
`/dev/kgsl-3d0` 的路径。做桥之前必须先补这两块（Phase 0）。

### 3.4 FCL 机制调研结论

FCL 源码克隆在 `exp-opengl-driver/.research/fcl`（823 MB，可删）。

- 渲染器插件：v1（`fclPlugin` meta-data：`renderer`/`pojavEnv`/`boatEnv`）和
  v2（`fclPlugin_V2` + JSON 资源）。`RendererManager` 内置 6 个渲染器。
- 内置 **Freedreno** 渲染器 = Mesa **23.0.4** 的 `libOSMesa_8.so` +
  `POJAV_RENDERER=gallium_freedreno` → 这就是我们替换的对象。
- `FCL/src/main/jni/ctxbridges/egl_loader.c` 是“自定义 loader”：按
  `POJAVEXEC_EGL`（来自 renderer 的 `eglName`）dlopen 任意 EGL，Zink 就是这么用
  `libEGL_mesa.so` 的。
- 但 loader 只决定“加载哪个 EGL”，**不决定上下文 API**：`egl_bridge.c` 按
  `POJAV_RENDERER` 写死分支；没有任何分支是“EGL + 桌面 GL + freedreno”。
- `osm_bridge.c` 是 FCL 自带的 OSMesa 桥（内存帧 → `ANativeWindow_lock` +
  memcpy），不是我们写的。

---

## 4. 关键决策与原因

| 决策 | 原因 |
|---|---|
| OSMesa 选 25.0.7 | 最后一个带 OSMesa 的版本；GL 4.6 + a730v3 已具备 |
| 驱动侧不编 EGL | 满足“不用 EGL”；OSMesa 不需要 EGL |
| KGSL 后端回移 | 上游 Gallium 无 KGSL，否则 OSMesa 只能建 softpipe/llvmpipe |
| 去掉 libcutils/libsync | Android 上非公开系统库，App 加载有风险 |
| 不用 LSPosed 解决 | LSPosed 只能改 Java 配置，补不了 native 的 OSMesa/上下文 |
| 暂不把 OSMesa 回移 26.3 | 25.0.7 已能出 GL 4.6；26.3 回移是可选优化 |
| 不搭桥（当前） | 用户选择先走 OSMesa；桥方案见 §6 |

**已明确的冲突**：`不用 OSMesa + 不搭桥 + freedreno + 桌面 GL 4.6` 四者不可能
同时成立——无 OSMesa 就没有 freedreno 桌面 GL 前端；不加桥就没有其它接入点。

---

## 5. 风险 / 未验证

1. **FCL 插件未上真机**（最大风险）：KGSL Gallium 回移是 24.1 代码适配到 25.0.7，
   出错点可能在 `fd_pipe`/`submit`/`bo`。
2. FCL 版本对 v1 渲染器插件的支持（`fclPlugin`）。
3. `GL_VERSION` 是否真的上报 4.6（caps 代码在，但需设备确认）。
4. 26.3 EGL 的桌面 GL + `ANativeWindow` 组合上游未测试。
5. NDK 还有 24 个 host-only 占位文件（python/lldb），不影响交叉编译。

---

## 6. 下一步

### A. 真机测试（你做）
装 APK → FCL 版本设置 → 选 **Freedreno-A730**。失败时收集：

```bash
adb logcat -d | grep -E "GLBridge|OSMesa|libOSMesa|kgsl|EGL|DEBUG"
```

回退：切回内置 **Freedreno** 渲染器或卸载插件。

### B. 如果 25.0.7 失败
- 先看是否 `unsupported device: kgsl` / `fd_pipe_new` 失败（KGSL 回移问题）；
- 再决定修 25.0.7 还是转 §6.C。

### C. 桥方案（已评估，未执行，约 3–5 小时纯机器时间）
目标：FCL 新 renderer 走自编 Mesa EGL → 桌面 GL 4.6 → 直接绑 `ANativeWindow`。

- **Phase 0**：26.3 回移 KGSL Gallium 后端 + EGL/DRI 加 `/dev/kgsl-3d0` 打开钩子
  + 打包单库（或 bundle）；
- **Phase 1**：最小 aarch64 原生测试（eglBindAPI(OPENGL)、4.6 core context、
  `eglGetProcAddress`、swap），这是止损点；
- **Phase 2**：改 FCL `egl_bridge.c`/`gl_bridge`/`egl_loader` + 重编 JNI/重签 APK；
- **Phase 3**：真机联调 WSI（gralloc/sRGB/fence/resize）。

时间：纯构建编码 3–5 h；含真机迭代 5–9 h；若 FCL 只能走完整 Gradle，再加 2–4 h。
Token 约 80k–150k（Flash，成本可忽略）。

### D. 可选：OSMesa 回移到 26.3
把 25.0.7 的 `frontends/osmesa` + `targets/osmesa` + meson 选项搬到 26.3，FCL 零改动。
适合“想用 26.3 新驱动、又不想搭桥”。

---

## 7. 命令速查

```bash
# 25.0.7 OSMesa 重建（增量）
cd /home/lcys/android/gpu-driver/exp-opengl-driver && ./build-freedreno-osmesa.sh

# 重打 FCL 插件（需要 fcl-plugin/android-29.jar）
cd /home/lcys/android/gpu-driver/plugin && ./build.sh

# 校验产物
cd /home/lcys/android/gpu-driver/exp-opengl-driver/release-25.0.7-a730 && cat SHA256SUMS
md5sum /home/lcys/android/gpu-driver/vulkan-driver/Mesa-Turnip-Builder/release-26.3.0-rc1-a730/*

# Turnip 重建（首次全量）
cd /home/lcys/android/gpu-driver/vulkan-driver/Mesa-Turnip-Builder
DEVICE_PROFILE=a730 MESA_VER=26.3.0-rc1 LTO_MODE=off BUILD_TAG=-A730 bash build-turnip.sh
```

---

## 8. 文件地图

```
gpu-driver/
├── README.md                            # 总览（先看这个）
├── docs/
│   ├── HANDOVER.md                      # 本文件
│   ├── NOTES-ubwc-artifacts.md          # UBWC 花屏排查记录
│   ├── NOTES-turnip-vs-gallium-ubwc.md  # 与 Turnip 的实现差异
│   └── README-FREEDRENO-EGL.md          # 26.3 EGL 直出说明
├── mesa/
│   ├── patches/                         # 改过的上游文件快照 + MAP（kgsl/ 有独立 README）
│   └── ...
├── plugin/                              # FCL 插件（唯一版本：pkg.freedreno.kgsl / 26.3.0-main）
│   ├── AndroidManifest.xml
│   ├── java/pkg/freedreno/kgsl/MainActivity.java   # 调试开关界面
│   ├── res/ icon/ build.sh
├── scripts/build-egl.sh                 # ninja 构建 Mesa 产物到 dist/
├── tools/                               # gltest / ab-mc.sh / flicker.py / egl_bridge_shim.c
├── dist/                                # 产物（不入库）
├── archive/                             # 旧版本/旧清单/日志（不入库）
├── exp-opengl-driver/gl_workdir/        # Meson 构建工作区（路径固定，别改名）
└── vulkan-driver/                       # Turnip 构建器 + Mesa 26.3 源码树（不入库）
```

---

## 9. 待用户确认

- **PTC 预设**：本会话工具集里没有 `run_code`，PTC 未生效；下一会话启用 PTC 后，
  按 §0 的方式执行本文件 §6 的步骤。
