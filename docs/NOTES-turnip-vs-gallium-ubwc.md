# Turnip(Vulkan) vs freedreno-Gallium(OpenGL)：UBWC 实现差异对照

> **复核结论（2026-11-01）**：本文是纯源码分析，D1–D11 仍然成立，但**实际的花屏根因不是
> UBWC**——是 KGSL 后端的 fence 等待超时单位错误（纳秒当毫秒），v1.0.1 修复后
> Core + 光影 + UBWC 下的图标噪点/透明全部消失（见 `docs/NOTES-ubwc-artifacts.md`）。
> 文中的 D2/D4/D6/D10 加固改动仍然保留在树里（作为正确性修补），不要因为“问题已修”
> 就把它们回滚。

纯源码分析，未上机。对象是同一棵 Mesa 26.3.0-rc3 树：

    src/freedreno/vulkan/…            Turnip
    src/gallium/drivers/freedreno/…   我们的 GL 驱动
    src/freedreno/fdl/…               两边共用的 layout / 描述符库
    src/freedreno/drm/…               gallium 的 BO 层（含我们从 25.0.7 移植回来的 kgsl）

## 0. 结论速览

26.3 把 UBWC 的**物理布局和纹理描述符**统一进了 `fdl`，Turnip 和 Gallium 用的是**同一份代码**。
所以"UBWC 偏移/描述符/对齐算错了"这类假设可以基本排除（和我们此前的实机实验一致）。

真正不同的是"**谁负责让 flag buffer 的内容和像素保持同步**"：

| | Turnip | Gallium |
|---|---|---|
| flag buffer 初值 | 不管（依赖内核给的新页是 0） | 惰性 GPU 清（`needs_ubwc_clear`） |
| BO 复用 | 没有 BO 缓存，永远是新分配 | 有 BO 缓存，会复用带陈旧 flag 的 BO |
| 可见性 | per-access 显式屏障（CCU clean ⇒ UCHE invalidate） | per-batch 依赖跟踪 + 每 batch 开头整批 invalidate |
| CPU 碰 UBWC 图 | 禁止（带 HOST_TRANSFER 的图直接不给 UBWC） | 允许（走 staging + GPU blit） |
| UBWC 资格 | 创建期按 usage/format 决定，宁可不给 | 创建期按 bind/format 决定，事后可降级（demote/uncompress） |

下面 D1–D11 是逐条差异。

## 1. 完全共享、可以排除的部分

- `fdl6_layout_image()`（`src/freedreno/fdl/fd6_layout.c`）
  - UBWC meta 放在像素数据**之前**（`slices[level].offset += ubwc_layer_size * array * depth`）
  - 每层 4K 对齐、`UBWC_PLANE_SIZE_ALIGNMENT`、mip 时 `ubwc_width0/height0` 取 2 的幂、`RGB_TILE_HEIGHT_ALIGNMENT` 64/16
  - linear fallback（`FDL_LINEAR_FALLBACK_THRESHOLD`）、`tile_all`、`force_ubwc`
- `fdl_ubwc_pitch()` / `fdl_ubwc_offset()` / `fdl_ubwc_enabled()`（per-level）
  - Gallium 侧 `fd_resource_ubwc_enabled()` 就是 `fdl_ubwc_enabled(&rsc->layout, level)`（`freedreno_resource.h:348`），**没有**偷懒用 `layout.ubwc`
- `fdl6_view_init()`（`src/freedreno/fdl/fd6_view.cc`）：纹理描述符
  - `TEX_MEMOBJ_3_FLAG`、`7/8` = flag buffer 地址、`9.FLAG_BUFFER_ARRAY_PITCH = ubwc_layer_size>>2`、`10.FLAG_BUFFER_PITCH/LOGW/LOGH`
  - Gallium 的 `fd6_texture.cc:553-560` 直接调 `fdl6_view_init()` 拷 `view.descriptor`
- `fd6_format_table.c`（`fd6_color_format` / `fd6_tile_mode`）—— 同一个表
- A2D 的 flag 寄存器：blit 时两边都设 `RB_A2D_DEST_FLAG_BUFFER_BASE/PITCH`（Turnip `r2d_dst`+`tu_cs_image_flag_ref`，Gallium `emit_blit_dst`），GMEM resolve 时两边都设 `RB_RESOLVE_SYSTEM_FLAG_BUFFER_BASE/PITCH`
- 设备属性（`freedreno_devices.py` 的 `has_ubwc_linear_mipmap_fallback`、`ubwc_unorm_snorm_int_compatible`、`supports_uav_ubwc`、`has_8bpp_ubwc`、`has_gmem_fast_clear`…）两边读同一个 `fd_dev_info`

**所以：flag 地址/步长/描述符位/布局 → 不是差异源。**

## 2. 差异清单

### D1. BO 生命周期：BO 缓存 vs 每次新分配 【结构性差异，最大】

- Turnip：`tu_bo_finish()`（`tu_knl.cc:124`）→ `knl->bo_finish()` 直接销毁，**没有 userspace BO cache**（全树 grep 不到 `try_recycle`/`bo_cache`）。KGSL 新分配的内存是清零的，所以 Turnip 拿到的 flag buffer 初值永远是 0。
- Gallium：`src/freedreno/drm/freedreno_bo.c:345 try_recycle()` → `fd_bo_cache_free()`；`fd_bo_del()` 先把 BO 塞进 `dev->bo_cache`，下次 `fd_bo_new()` 直接捞出来用，**里面的 UBWC flag 是上一个使用者留下的**。

后果：Gallium 必须自己保证"用之前把 flag 区清零"，而这正是 D2 的事；Turnip 不需要。

### D2. flag buffer 初值：【Gallium 独有机制，且有漏洞】

- Gallium：`realloc_bo()` 分配 BO 后，若 `layout.ubwc` 就置 `rsc->needs_ubwc_clear = true`（`freedreno_resource.c:208-217`，注释自己写了"BO 从 cache 回收时 flag 区是垃圾，这是 UBWC 间歇性失败的根源"）。真正清零发生在 `fd_batch_write_prep()`（`freedreno_resource.h:411-417`）→ `ctx->clear_ubwc()` → `fd6_clear_ubwc()`（`fd6_blitter.cc:574`，用 A2D 把 `slices[0].offset` 字节写 0）→ 塞进 `batch->prologue`。
- 触发点只有 `fd_batch_resource_write()` 尾部（`freedreno_batch.c:576`）。也就是说：**只有"第一次 GPU 写"才会清 flag**。
- Turnip：没有对应机制（`tu_image_init` 里没有任何 memset/clear）。

后果（Gallium 侧漏洞）：
1. 如果一个 UBWC 资源**第一次使用是 GPU 读**（采样/作为 blit 源），flag 还是陈旧的 → 直接按陈旧 flag 解压。
2. `fd_batch_resource_write()` 的跨 context 分支 `return` 在 `fd_batch_write_prep()` **之前**（`freedreno_batch.c:559-563`），此时 flag 永远不清。
3. `needs_ubwc_clear` 是"清一次就完事"，但如果之后 BO 被复用而没走 `realloc_bo()`（例如 `fd_bo` 层面复用），不会重新置位。

### D3. "谁配拥有 UBWC"

- Turnip `ubwc_possible()`（`tu_image.cc:324-430`）：按 **VkImageUsage/format/samples/mip** 判定，且大量"宁可不给"的条件：sparse、compressed、E5B9G9R9、`S8_UINT`、snorm（老 GPU）、8bpp 无 8bpp-ubwc、3D+mip、storage image 无 uav_ubwc、HOST_TRANSFER、mutable 且 format list 不兼容、`VK_IMAGE_COMPRESSION_DISABLED_EXT`、`TU_DEBUG(NOUBWC)`。
- Gallium `get_best_layout()`（`freedreno_resource.c:1265-1355`）：只在**创建期**看 `PIPE_BIND_*`/`PIPE_USAGE`；`ok_ubwc_format()`（`fd6_resource.cc:24-114`）看格式。**没有** "会不会被 CPU 访问"、"会不会被重解释" 的判定。

### D4. 格式重解释：事后降级 vs 创建期禁用 【结构性差异】

- Turnip：创建期就决定。mutable image 若 format list 不满足 `tu6_mutable_format_list_ubwc_compatible()`，直接 `ubwc_enabled = false`（`tu_image.cc:606-664`）；两个 UBWC 图之间格式不同就 `use_staging_blit = true`（`tu_clear_blit.cc:3472-3480`，注释 "Both formats use UBWC and so neither can be reinterpreted"）。
- Gallium：允许。`fd6_check_valid_format()` 在**每次绑定/采样**时检查，不合法就 `fd6_validate_format()` → `fd_resource_uncompress()` → `fd_try_shadow_resource()` 做一次**资源影子替换**（`SWAP(rsc->bo)`、`SWAP(rsc->layout)`、搬 batch 引用、再 blit 解压），`rsc->seqno` 递增，所有 sampler view / FB 绑定被 `rebind_resource()` 修。

后果：Gallium 的 demote 路径是"运行时换 BO"，比 Turnip 的"创建期就不给"危险得多。只要 MC 对图集做过一次"不同 format 的采样/绑定"（例如 RGBA8 图集被当 `B8G8R8A8`、或 `*_SRGB` 与 `*_UNORM` 混用、或 R8G8↔R16 互相看），就会触发一次 BO 交换 + 全量 blit；而缓存过的描述符/状态如果没被完全 invalidate，就会采到**旧 BO**（旧 BO 里的内容正是同一张图集的另一块/旧版本 → 视觉上像"别的图集区域"）。

### D5. `is_mutable` / `TEX_MEMOBJ_1_MUTABLEEN`【Gallium 缺口，明确】

- Turnip 把 `is_mutable` 一路传进 `fdl6_layout_image()`（`params.is_mutable`），`fdl6_view_init()` 据此决定 `A6XX_TEX_MEMOBJ_1_MUTABLEEN`（`fd6_view.cc:281-294`）。
- Gallium 的 `fd_image_params()`（`freedreno_resource.h:363-380`）**根本没有 `is_mutable` 字段**（全树 grep `is_mutable` 在 gallium 目录下 0 命中）→ `layout.is_mutable` 恒为 false。

严格说这是"Gallium 不打算支持格式重解释，用 D4 的 demote 代替"，但两者组合起来意味着 **GL 侧的"MUTABLEEN"语义和 Turnip 不同**。

### D6. CCU ↔ UCHE 可见性 【语义差异，可能是我们的 bug】

- Turnip：`tu_flush_for_access()`（`tu_cmd_buffer.cc:5774-5813`）用宏把"clean 一个域"和"invalidate 所有其它域"绑在一起：

      SRC_FLUSH(CCU_COLOR, CCU_CLEAN_COLOR, CCU_INVALIDATE_COLOR)

  展开 = `CCU_CLEAN_COLOR | (ALL_INVALIDATE & ~CCU_INVALIDATE_COLOR)`，也就是**每次清 CCU 都会顺带 invalidate UCHE**。另外 `tu6_clear_lrz()` 也显式写 `CCU_CLEAN_COLOR | CACHE_INVALIDATE | WAIT_FOR_IDLE`（`tu_clear_blit.cc:2475`），blit/clear 的收尾注释也写明"Draws write to the CCU, unlike CP_EVENT_WRITE::BLIT which writes to sysmem"（`tu_clear_blit.cc:6074`）。

- Gallium：`fd6_emit_flushes()`（`fd6_barrier.cc:18-40`）里 `FD6_FLUSH_CCU_COLOR` **只发** `FD_CCU_CLEAN_COLOR`，不补 `FD_CACHE_INVALIDATE`。`handle_rgba_blit()` 收尾是 `FLUSH_CCU_COLOR|FLUSH_CCU_DEPTH|FLUSH_CACHE|WAIT_FOR_IDLE`（`fd6_blitter.cc:1456-1460`）—— `FD_CACHE_CLEAN` 是写回，不是 invalidate。

Gallium 的补偿是"每个 batch 开头整批 invalidate"：`fd6_emit_restore()` 在 A6XX 上发 `fd6_cache_inv()`（`fd6_emit.h:291-297`）= `FD_CCU_INVALIDATE_COLOR + FD_CCU_INVALIDATE_DEPTH + FD_CACHE_INVALIDATE`（`fd6_emit.cc:1090-1111`），每个 batch 一次，A7XX 走另一条等价分支。**batch 内部**没有等价物。因此"在同一个 batch 里先经 CCU 写、再经 UCHE 读"就漏了 —— 这正是我们此前用"同 batch 读写就 flush"修掉 mob 闪烁时踩到的同一类问题，只是那处修的是 batch 依赖，不是 cache 语义。

### D7. 同步粒度：per-access vs per-batch

- Turnip：每个 access（shader read / color write / transfer read …）都过 `tu_flush_for_access`，屏障按需插入。代价是驱动自己必须"知道"每个 image 的 UBWC 状态。
- Gallium：`fd_resource_tracking`（`track->write_batch`、`batch_mask`）+ `fd_batch_resource_read_slowpath()` 决定 flush 谁。**依赖判断靠"资源"，不靠"访问类型"**，所以任何"同一 batch 内的 RAW/CCU-UCHE 危险"都不会被这条路径发现。

补充（我们这棵树里已确认）：`fd_batch_needs_flush()` 在 26.3 里只写不读（`freedreno_batch.h:396` 只赋值，唯一的"读"在 debug printf 里），所以任何指望它触发 flush 的地方都是空操作 —— 这条已经记在 `freedreno_batch.c:615` 的注释里。

### D8. CPU 访问 UBWC 图

- Turnip：`ubwc_possible()` 直接把 `VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT` 的图判为不可 UBWC（`tu_image.cc:400-404`，"We don't support compressing or decompressing on the CPU"）。CPU 上传只走 `vkCmdCopyBufferToImage`（GPU blit）或线性图。
- Gallium：图集一律 UBWC + tiled，CPU 上传走 `resource_transfer_map()` → `resource_transfer_map_staging()`（`freedreno_resource.c:790-830`，为 tiled 资源强制 staging）→ unmap 时 `fd_blit_from_staging()`；且 `do_blit()` 在 `fd_blit()` 返回 false 时会退化成 `util_resource_copy_region()`（CPU 路径）。
- 另：Turnip 从 KGSL 读 `KGSL_PROP_HIGHEST_BANK_BIT` / `KGSL_PROP_UBWC_MODE` 填 `device->ubwc_config`（`tu_knl_kgsl.cc:1945-2057`，含 663/680 的硬编码 quirk）—— 这个 config 只用于 **CPU 侧 tiled memcpy**（`fdl6_memcpy_linear_to_tiled`）。**Gallium 全树没有任何 `ubwc_config`/`highest_bank_bit`**，因为它从不做 CPU tiled 访问。→ 这一条是"设计不同"，不是 bug，但解释了为什么 Turnip 需要那套 KGSL 属性。

### D9. GMEM/CCU 布局与 fast clear

- Turnip：`tu_choose_gmem_layout()` 会在"非对齐 store / FDM / S8 resolve / 自定义 resolve / mutability 不匹配"时切到 `TU_GMEM_LAYOUT_AVOID_CCU`（`tu_clear_blit.cc:6147-6195`），即**不用 CCU 空间当 GMEM**；并用 `tu_emit_cache_flush_ccu(TU_CMD_CCU_GMEM|SYSMEM)` 在 gmem↔sysmem 切换时保证 clean+invalidate+`RB_CCU_CNTL` 重编程。
- Gallium：`fd6_emit_gmem_cache_cntl(cs, screen, gmem)` + `fd6_emit_sysmem_fini` 里把 `FD6_FLUSH_CCU_COLOR|INVALIDATE_CCU_COLOR|INVALIDATE_CCHE|FLUSH_CACHE|WAIT_FOR_IDLE` 挂在 `batch->barrier` 上（`fd6_gmem.cc:2479-2486`）。没有 `AVOID_CCU` 这种布局选择，靠 blitter 端 `emit_setup()` 每次 blit 前无条件 flush+invalidate 两个 CCU 域来兜底。

### D10. GMEM resolve 源端的 flag 寄存器残留【Gallium 小缺口】

- `resolve_tile_setup()`（`fd6_blitter.cc:1287-1366`）用 `TPL1_A2D_SRC_TEXTURE_*` 描述 GMEM 源。YUV 分支会显式写 `FLAG_BASE(0)/FLAG_PITCH(0)` 并注释 "Always emit FLAG_BASE/PITCH for YUV to clear stale values"；**非 YUV 分支什么都不写**，于是 `TPL1_A2D_SRC_TEXTURE_FLAG_BASE` 保留上一次 A2D 操作（很可能是图集上传 blit）留下的 UBWC 地址。
- Turnip 的 `r2d_src()` 无条件发 `__TPL1_A2D_SRC_TEXTURE_FLAG_BASE` 的 3 个 dword（`tu_clear_blit.cc:563`）。

目前看 `A2D_SRC_TEXTURE_INFO` 的 ubwc 位没置时 HW 不该去读它，所以危害等级低，但"靠寄存器残留碰运气"本身不合格。

### D11. 其它零散差异

| 项 | Turnip | Gallium |
|---|---|---|
| 关闭 UBWC | `TU_DEBUG=noUbwc` | `FD_MESA_DEBUG=noubwc` |
| 单图禁用压缩 | `VK_IMAGE_COMPRESSION_DISABLED_EXT` 支持 | 无 |
| `has_coherent_ubwc_flag_caches==false` 时的 primitive-order 处理 | 用 `prim_mode_sysmem` 动态状态兜（`tu_pipeline.cc:4104/4368`, `tu_cmd_buffer.cc:1235/1247`） | **完全没有这个概念** |
| sparse / 3D+mip / mutable 的 UBWC | 创建期精细判定 | 无（靠 demote） |
| UBWC 与 MSAA | `ubwc_possible()` 只在 `!has_z24uint_s8uint` 时禁 D24S8 | `ok_ubwc_format()` 对 Z24X8 要求 `has_z24uint_s8uint || samples<=1` |

## 3. 用这份对照去收窄我们的 bug

现有实验事实（来自 `NOTES-ubwc-artifacts.md`）：

1. `FD_MESA_DEBUG=noubwc` → 图集完全正常
2. `FD_MESA_DEBUG=sysmem` → 物品正常
3. 合成用例（2048²/1152² per-cell 上传 + GenerateMipmap、MIPS、CU、MIPLOD、TP+blit 回读、stale-descriptor 探针 0 命中）全过
4. 同 batch 读→flush 修好了 mob/牛羊毛闪烁，但没修好图集

关键逻辑：**`sysmem` 不改变任何纹理布局**。全树 `FD_DBG(SYSMEM)` 只有一个消费者 `freedreno_gmem.c:781`（把整个 batch 的 `sysmem` 置 true，从而不进 GMEM tile pass），`get_best_layout()` 完全不看它。所以如果事实 2 成立且稳定，脏东西就出在"UBWC 当渲染目标 + GMEM"这条路上（D9/D6），而不是"图集采样"（D1/D2）。但"局部矩形 + 像别的图集区域"这个形态只能由采样来解释——两者矛盾。

**所以第一个该做的不是改代码，是把事实 2 钉死**（一次 A/B 就能把两类假设劈开）：

- 基线 `FD_MESA_DEBUG=sysmem`（UBWC 仍开）
- `FD_MESA_DEBUG=notile`（tiled 但无 UBWC）
- `FD_MESA_DEBUG=noubwc`（线性、无 UBWC）

若 `notile` 干净而 `sysmem` 也干净 → 问题在 UBWC 语义（D1/D2/D6）；
若 `notile` 也脏 → 问题在 tiling，跟 UBWC 无关，我们的方向从一开始就偏了。

## 4. 可以现在就改的四个低风险缺口

按"不改变现有行为、只补缺口"的标准：

1. **`fd_batch_write_prep()` 提到 early-return 之前**（`freedreno_batch.c:512-577`）：跨 context 分支现在直接 `return`，flag 永远不清。
2. **读路径也吃 `needs_ubwc_clear`**：在 `fd_batch_resource_read_slowpath()` 里，若 `rsc->needs_ubwc_clear` 就先 flush+清（等价于把 `fd_batch_write_prep` 的语义从"写前"扩成"用前"）。这直接覆盖 D2 漏洞 1。
3. **`fd6_emit_flushes()` 补 UCHE invalidate**：当 `!has_coherent_ubwc_flag_caches` 且发出 `FD_CCU_CLEAN_*` 时，追加 `FD_CACHE_INVALIDATE`，对齐 Turnip `SRC_FLUSH` 的语义（D6）。
4. **`resolve_tile_setup()` 非 YUV 也清 `A2D_SRC_FLAG_BASE/PITCH`**（D10），保持和 YUV 分支一致。

第 1/2 条最可能直接命中图集问题：它把"图集第一次被采样时 flag 还是 BO 缓存里的垃圾"这个窗口彻底关掉，而且不需要任何设备实验就能判断是否影响现有合成用例。

## 5. 已实现（26.3 树 + kgsl263 分支）

四条都按上面的顺序落地了，改动只碰 gallium 侧，不动 Turnip：

| # | 文件 | 改动 |
|---|---|---|
| 1 | `freedreno_batch.c` `fd_batch_resource_write()` | `fd_batch_write_prep()` 移到所有 early-return 之前（含跨 context 分支） |
| 2 | `freedreno_batch.c` `fd_batch_resource_read_slowpath()` + `freedreno_resource.h` | 读之前也清 flag；`fd_batch_write_prep()` 里加 `batch->flushed` 保护（已提交的批次不能再塞命令，flag 位保留给下一个批次） |
| 3 | `a6xx/fd6_barrier.cc` `fd6_emit_flushes()` | 任何 CCU clean/invalidate 自动补 `FD6_INVALIDATE_CACHE \| FD6_WAIT_FOR_IDLE`（a6xx 上 invalidate 需要 WFI 才落地，和 turnip 在 a6xx 换 CCU 状态时的做法一致） |
| 4 | `a6xx/fd6_blitter.cc` `resolve_tile_setup()` | `A2D_SRC_FLAG_BASE/PITCH` 对所有格式都清零（原来只在 YUV 分支），ncrb 预留 34→37 dword |

未做的一条：**D5 的 `is_mutable`**。查过 `freedreno_devices.py`：`ubwc_all_formats_compatible` 只在 a7xx gen3+ 为真，a6xx（含我们的 a730）为假；
Turnip 在 a6xx 上因此走的是"mutable 图干脆不给 UBWC"，`layout.is_mutable` **实际也是 false**。
如果在 a6xx 上把 `MUTABLEEN` 打开，`fd6_texture_swap()` 会从 `WZYX` 变成表里的 swap，而 RB/A2D 侧仍然用 `WZYX`
（`fd6_color_swap(..., false)`），数据存储顺序和采样顺序就对不上了 —— 那是**引入** bug，不是修 bug。
结论：这条在 a730 上不适用；真要做，得把 `is_mutable` 同时贯穿 `emit_mrt`/`emit_blit_dst`/`emit_blit_src` 和 descriptor，等有 a7xx 设备再说。

## 6. 第二轮：demote 路径上的一个真 bug

前面四条只是硬化。继续读 `fd6_validate_format` → `fd_resource_uncompress` → `fd_try_shadow_resource` 这条链时，
发现缓存纹理状态在 BO 被换掉之后**不会被失效**：

- `fd_resource_set_bo()`（以及 shadow 里的 `rsc->seqno = seqno_next_u16()`）会把 `rsc->seqno` 换成**新值**；
- 而 `fd6_rebind_resource()`（`a6xx/fd6_texture.cc:899`）判的是
  `if (rsc->seqno == state->view_rsc_seqno[i]) tex->invalidate = true;`，
  其中 `state->view_rsc_seqno[i]` 是建状态对象时记下的**旧值**；
- 两个调用点（`invalidate_resource()` 和 `fd_try_shadow_resource()`）都是**先换 BO 再 rebind**，
  所以这个比较永远不成立 → 引用了该资源的 `fd6_texture_state` 不会被标脏 →
  之后 `fd6_texture_state()` 命中缓存，继续用**旧 BO 的 TEX 描述符**。

旧的 BO 在 `fd_resource_uncompress()` 结束时随 shadow 一起 `pipe_resource_reference(NULL)` 释放，
回到 BO cache 后可能马上被别的纹理拿走 —— 于是采样到的是**别的纹理的内容**。
这正好对上"图集局部出现别处的条纹"；而 `noubwc` 下 `fd6_check_valid_format()` 第一行就
`if (!rsc->layout.ubwc) return FORMAT_OK;`，根本不走 demote，所以干净。

修法：把 `rebind_resource(rsc)` 挪到 BO 变更**之前**（两处），此时 `rsc->seqno` 还是缓存状态记录的那个值，
比较成立、状态被正确标脏；shadow 里原来那句（换完之后再 rebind）删掉。
`rebind_resource()` 只置脏位，真正的 emit 在后续 draw 时才发生，那时读的是新 BO，顺序安全。

构建：`scripts/build-egl.sh` 通过，产物在 `dist/`（v1.0.2 的
`libgallium_dri.so` sha256 `3a09a6cd…`），改过的源文件同步在 `mesa/patches/`。
