# MC 26.3 UBWC 花屏排查记录（阶段性）

## 判定手段（都可用，不需要人盯）

- 插件 A/B：`/sdcard/FCL/fcl_ab.txt`（KEY=VALUE，force-stop FCL 后生效）
- 截图 → 裁热栏：`convert shot.png -crop 1300x140+880+1650 +repage -resize 300% hotbar.png`
  - **最佳判据**：UBWC 下热栏里会有几格变成不相干图集的横条纹；`FD_MESA_DEBUG=noubwc` 下完全正常
- gltest：`tools/gltest`，logcat 标签 GLTEST
  - `UBWC-ATLAS`（2048²+逐格上传+mip，blit 与 TP 双路校验）
  - `UBWC-MIPLOD`（逐层采样；**宽度<4 的线性回退层仍有独立 bug**，8 层形态全过）
  - `UBWC-CU`（先 clear 再局部上传）

## 已修

- **实体/生物闪烁（牛、羊羊毛）**：`fd_batch_resource_read_slowpath()` 补「同批次写后读 → `fd_batch_flush()`」。
  注意：`fd_batch_needs_flush()` 在本版本里**没有任何读者**，别用它。

## 已排除（都有证据）

| 嫌疑 | 证据 |
|---|---|
| UBWC flag/layout/描述符 | 自检全绿 + 代码核对 |
| mip 采样（含 MC 图集形态 1024²×5） | PASS |
| 图集逐格上传 + GenerateMipmap（blit 与 TP 两路） | PASS 1024/1024 |
| 先 clear 再局部上传 | PASS |
| 描述符陈旧（资源 realloc 后） | 插桩检查 0 次命中 |
| BO 提前复用 | 新老策略都坏；且 `no_recycle` 会让主菜单背景变黑（未解） |
| LRZ / 缓存模式 / cpu_prep | 各自关掉后现象不变 |

## 仍开着

1. **物品图标串图（UBWC only）**：截图可见，noubwc 正常。合成用例都复现不出来，
   说明差异在 MC 特有的用法（例如图集多次重建/多尺寸 sprite/SRGB/sampler 状态）。
2. **云闪烁**：`sysmem` 下仍闪 → 与 GMEM 无关；`nobin` 的结论还没拿到。
