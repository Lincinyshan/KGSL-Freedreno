# KGSL Gallium backend — 26.3 用

来源：<https://github.com/SolDev69/freedreno-gallium-mesa>
分支 `feat/mesa-update/24.1`（Mesa 24.1.2 + KGSL Gallium 支持），
文件 `src/freedreno/drm/kgsl/{kgsl_bo.c,kgsl_device.c,kgsl_pipe.c,kgsl_priv.h,kgsl_ringbuffer_sp.c}`。

这是"最后一个有 KGSL Gallium 实现"的版本。上游 Mesa 在 19.3 / 21.3 / 24.1 / 25.0 / 26.2 / 26.3
都没有这个后端（`src/freedreno/drm/` 只有 `msm` 和 `virtio`），所以 26.3 只能用它。

## 相对原版的全部改动（只有 3 处，其余逐字节一致）

1. `kgsl_device.c`：删掉 `dev->features = FD_FEATURE_DIRECT_RESET | FD_FEATURE_IMPORT_DMABUF;`
   —— 26.x 的 `struct fd_device` 没有 `features` 字段。
2. `kgsl_pipe.c`：删掉 `kgsl_reset_status()` 和 `.reset_status` 设计符
   —— 26.x 的 `struct fd_pipe_funcs` 里没有这个钩子。
3. `kgsl_ringbuffer_sp.c`：加 `kgsl_bo_gpuaddr()`，用 `fd_bo_heap_block(bo)->iova` 作为
   suballoc BO 的基址。26.x 的 `submit_offset()` 已经包含 suballoc 偏移
   （`freedreno_priv.h`），再用 `bo->iova` 加会重复计算，导致 IB 地址错误
   （`CP opcode error opcode=0x00000000` + 每次提交挂 GPU）。

## 不再有的东西（我们之前的自研改动已全部移除）

- `kgsl_pending_*` 延迟释放队列、`FCL_BO_SAFE` / `no_recycle`
- `FCL_NO_CPU_PREP`、`FCL_BO_WC`（原版 `kgsl_bo_cpu_prep` 是空实现，不设 cache mode）
- `kgsl_note_submit` / `kgsl_wait_ts`
- `kgsl_bo.c` 里的 `[fcl-dri]` 调试打印

## 挂接点（不属于本目录，属于移植）

- `src/freedreno/drm/freedreno_device.c`：`#if HAVE_FREEDRENO_KGSL` 前向声明 +
  `drmGetVersion()` 失败时回退到 `kgsl_device_new()`。
- `src/freedreno/drm/meson.build`：`freedreno-kmds=kgsl` 时编译 `kgsl/` 并定义
  `HAVE_FREEDRENO_KGSL=1`。
- `src/egl/drivers/dri2/platform_android.c`：没有 DRM 节点时直接 open `/dev/kgsl-3d0`
  并让 loader 用 `kgsl` 驱动名。
