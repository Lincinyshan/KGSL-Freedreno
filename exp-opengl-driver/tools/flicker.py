#!/usr/bin/env python3
"""统计同一静止画面连拍的闪烁程度。

用法: flicker.py <prefix> <count>
输出:
  MAD   —— 相邻两帧的平均绝对差（整体抖动）
  FLK   —— 时间维度标准差 > 12 的像素占比（局部闪烁）
  BBOX  —— 闪烁像素的包围盒（缩放到 2880x1800）
另外把闪烁掩码写到 <prefix>-mask.png，方便直接看位置。
"""
import sys
import numpy as np
from PIL import Image

prefix = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 8

frames = []
for i in range(1, n + 1):
    im = Image.open(f"{prefix}-{i}.png").convert("RGB")
    frames.append(np.asarray(im, dtype=np.int16))

a = np.stack(frames)                      # (n, H, W, 3)
mad = np.abs(np.diff(a, axis=0)).mean() if n > 1 else 0.0
std = a.std(axis=0).max(axis=2)           # (H, W)
mask = std > 12
frac = mask.mean() * 100.0

ys, xs = np.nonzero(mask)
if len(xs):
    h, w = mask.shape
    sx, sy = 2880.0 / w, 1800.0 / h
    bbox = (int(xs.min() * sx), int(ys.min() * sy),
            int(xs.max() * sx), int(ys.max() * sy))
else:
    bbox = None

print(f"frames={n} MAD={mad:.3f} FLK={frac:.3f}% BBOX={bbox}")

Image.fromarray((mask * 255).astype(np.uint8)).save(f"{prefix}-mask.png")
