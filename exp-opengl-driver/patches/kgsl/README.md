# KGSL Gallium backend (backport)

Mesa 25.0.7 upstream has **no KGSL backend for the Gallium (GL) driver** — only
Turnip (Vulkan) has `tu_knl_kgsl.cc`. The hooks in
`src/freedreno/drm/freedreno_device.c` (`#if HAVE_FREEDRENO_KGSL`) and the
`-Dfreedreno-kmds=kgsl` option are dead ends for Gallium, so a Gallium build
cannot open `/dev/kgsl-3d0` out of the box.

These files are backported from:

- Repo: <https://github.com/SolDev69/freedreno-gallium-mesa>
- Branch: `feat/mesa-update/24.1` (Mesa 24.1.2 + KGSL Gallium support)
- Files: `src/freedreno/drm/kgsl/{kgsl_device.c,kgsl_bo.c,kgsl_pipe.c,kgsl_ringbuffer_sp.c,kgsl_priv.h}`

Adapted for Mesa 25.0.7:

1. `kgsl_device.c`: removed `dev->features = FD_FEATURE_DIRECT_RESET | ...`
   (the `features` field / `FD_FEATURE_*` enum no longer exists in 25.0.7).
2. `kgsl_pipe.c`: removed `kgsl_reset_status()` and the
   `.reset_status = kgsl_reset_status` entry in `fd_pipe_funcs`
   (25.0.7's `fd_pipe_funcs` has no `reset_status`).
3. `src/freedreno/drm/freedreno_device.c`: added the `kgsl_device_new()`
   forward declaration under `#if HAVE_FREEDRENO_KGSL`.
4. `src/freedreno/drm/meson.build`: compiles the `kgsl/` directory and defines
   `HAVE_FREEDRENO_KGSL=1` when `freedreno-kmds` contains `kgsl`.

`kgsl_priv.h` includes `../../vulkan/msm_kgsl.h` (the Turnip KGSL uapi header),
which already exists in 25.0.7.
