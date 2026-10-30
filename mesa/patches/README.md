# Mesa 26.3 改动快照

这里的文件是 **我们改过的上游文件的完整副本**（不是 diff），来源树：
`vulkan-driver/Mesa-Turnip-Builder/turnip_workdir/mesa-mesa-26.3.0-rc1`。

应用方式：把文件按 `MAP` 里的对照 **覆盖** 回源码树；回滚就是 git checkout 上游版本。

`MAP` 一行一个文件：

```
platform_android.c   src/egl/drivers/dri2/platform_android.c
kgsl/kgsl_bo.c       src/freedreno/drm/kgsl/kgsl_bo.c
...
```

## 内容分组

| 组 | 文件 | 改了什么 |
|---|---|---|
| EGL 平台层 | `platform_android.c` `egl_dri2.c/h` `eglcurrent.h` `dri2_frontend.c` `pipe_loader_drm.c` `u_gralloc_internal.c` | 没有 DRM 节点时走 KGSL：直接 open `/dev/kgsl-3d0`、覆盖 `LIBGL_DRIVERS_PATH`、读插件设置文件 |
| freedreno 驱动 | `freedreno_screen.c` `freedreno_device.c` `freedreno_resource.c/h` `freedreno_batch.c` `freedreno_bo.c` `freedreno_priv.h` `fd6_resource.cc` `fd6_*.cc` `drm_meson.build` | `drmGetVersion()` 失败回退 KGSL、UBWC flag/同步修复、a7xx flush 对齐 |
| KGSL 后端 | `kgsl/` | 见 `kgsl/README.md`（fork 24.1.2 原版 + 3 处 26.x 适配） |
