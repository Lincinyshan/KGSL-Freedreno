# UBWC 花屏排查记录（结论版，2026-11-01）

> 最终结论：**我们花了大半时间追的“物品图标串图/透明”，根因不是 UBWC**，
> 而是 KGSL 后端的 fence 等待超时单位错误（纳秒当毫秒，超时放大 100 万倍）。
> 修复（v1.0.1）后，Core + 光影 + UBWC 下的图标噪点/透明全部消失。
> 本文保留排查过程与已排除项，避免后来者重复走一遍。

> [!WARNING]
> 本文结论**随时可能被推翻**：它们只是写这份文档时最强的证据。后续若出现新日志或新代码依据，以**实测与源码**为准，并直接更新文档（提交信息里写清依据）。

## 判定手段（都不需要人盯）

- 插件设置界面写 `Android/media/pkg.freedreno.kgsl/fcl_env.txt`，或
  `ADB="adb -s <serial>" tools/ab-mc.sh env "KEY=VALUE"`；改完 **force-stop FCL**；
- 截图 → 裁热栏：
  `convert shot.png -crop 1300x140+880+1650 +repage -resize 300% hotbar.png`
- `tools/gltest`（logcat 标签 `GLTEST`）：`UBWC-ATLAS` / `UBWC-MIPLOD` / `UBWC-CU`
- 开关：`noubwc`（UBWC 全关）、`sysmem`（绕 GMEM）、`flush`（同步）、
  `FCL_NO_FAST_CLEAR`（fast clear）

## 最终修复

| 问题 | 根因 | 修复 |
|---|---|---|
| 物品图标噪点/透明（Core + 光影 + UBWC） | `kgsl_pipe_wait()` 把纳秒超时当毫秒传给 `waittimestamp` | v1.0.1：`DIV_ROUND_UP(ns,1e6)` 并 clamp |
| `Failed to wait for frame completion` 崩溃 | 同上 | 同上 |
| 偶发长时间卡死 | 同上 | 同上 |

修好后再测：原版、Chocapic13 V6 Lite、物品栏、区块渲染均正常，**不再需要 `noubwc`**。

## 已修（历史上真实存在过的独立 bug）

- **实体/生物闪烁（牛、羊羊毛）**：`fd_batch_resource_read_slowpath()` 补「同批次写后读 →
  `fd_batch_flush()`」。注意：`fd_batch_needs_flush()` 在本版本里没有任何读者，别用它。
- **demote 路径采样到旧 BO**：`rebind_resource()` 必须在 BO 变更**之前**调用
  （`fd6_rebind_resource()` 比较的是旧的 `rsc->seqno`）。

## 已排除（都有证据，别重复怀疑）

| 嫌疑 | 证据 |
|---|---|
| UBWC flag/layout/描述符 | 与 Turnip 共用 `src/freedreno/fdl`，自检全绿 |
| mip 采样（含 MC 图集形态 1024²×5） | PASS |
| 图集逐格上传 + GenerateMipmap（blit 与 TP 两路） | PASS 1024/1024 |
| 先 clear 再局部上传 | PASS |
| 描述符陈旧（资源 realloc 后） | 插桩 0 次命中；且 demote 修复后已覆盖 |
| BO 提前复用 | 新老策略都坏；`no_recycle` 会让主菜单背景变黑（未解，已放弃该方向） |
| LRZ / 缓存模式 / cpu_prep | 各自关掉后现象不变 |
| GMEM / fast clear | `sysmem` 下仍会花；花屏随 fence 修复消失 |

## 非驱动问题（别混进来）

- **区块锯齿状空洞**：原版「区块构建器：线程化」的官方说明就是“破坏方块时短暂出现图像空洞”，
  光影下编译慢会更明显。破坏方块触发重建，所以看起来“一破坏就好了”。
- **启动期卡顿**：区块加载 → 大量分配 → GC/JIT，2–3 分钟后自然恢复；
  用 `adb shell top -H -b -n 1 -p <pid>` 可看到 G1/C2 线程占用。

## 相关文档

- `docs/NOTES-turnip-vs-gallium-ubwc.md`：与 Turnip 的逐条实现对照（D1–D11）
- `docs/HANDOVER.md` §7：按时间排的踩坑与修复
