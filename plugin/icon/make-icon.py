#!/usr/bin/env python3
"""Generate the FCL renderer-plugin icon.

Two designs:

  chip  (default) — "B": GPU die outline with pins, white arrow piercing in
                    from the left, tip stopping at the die centre.
  mesa            — "C": a flat-topped mesa silhouette against a dusk sky,
                    with a thin ring above it.

Usage:
  python3 make-icon.py            # chip -> writes ../res/mipmap-*/
  python3 make-icon.py mesa       # mesa -> writes preview/icon-C-*.png only
  python3 make-icon.py mesa --res # mesa -> also writes ../res/mipmap-*/
"""
import os
import sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "res")
PREVIEW = os.path.join(HERE, "preview")
SIZE = 512
DENSITIES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}

# --- shared ---
BG_TOP = (18, 22, 28)
BG_BOTTOM = (9, 12, 16)
FRAME = (42, 51, 63)
CHIP = (245, 166, 35)
CHIP_FILL = (26, 18, 7)
PIN = (90, 102, 117)
ARROW = (245, 247, 250)

# --- "mesa" design ---
SKY_TOP = (24, 14, 48)
SKY_MID = (118, 40, 82)
SKY_HORIZON = (232, 104, 44)
GROUND = (10, 7, 14)
MESA_FRONT = (9, 6, 12)
MESA_BACK = (40, 26, 46)
RING = (253, 230, 138)


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size - 1, size - 1], radius, fill=255)
    return m


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def gradient(size):
    grad = Image.new("RGBA", (size, size))
    gd = ImageDraw.Draw(grad)
    for y in range(size):
        t = y / (size - 1)
        c = lerp(BG_TOP, BG_BOTTOM, t)
        gd.line([(0, y), (size, y)], fill=c + (255,))
    grad.putalpha(rounded_mask(size, int(size * 0.22)))
    return grad


def render_chip(S):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    img.alpha_composite(gradient(S))
    d = ImageDraw.Draw(img)

    d.rounded_rectangle([10, 10, S - 11, S - 11], int(S * 0.20),
                        outline=FRAME + (200,), width=4)

    cx, cy = S // 2, S // 2
    half = int(S * 0.205)
    stroke = int(S * 0.035)
    rad = int(S * 0.10)

    pin_len, pin_thick, gap = int(S * 0.055), int(S * 0.030), int(S * 0.017)
    spread = int(S * 0.11)
    for off in (-spread, 0, spread):
        for x0, x1 in (
            (cx - half - gap - pin_len, cx - half - gap),
            (cx + half + gap, cx + half + gap + pin_len),
        ):
            d.rounded_rectangle([x0, cy + off - pin_thick // 2, x1, cy + off + pin_thick // 2],
                                pin_thick // 2, fill=PIN + (255,))
        for y0, y1 in (
            (cy - half - gap - pin_len, cy - half - gap),
            (cy + half + gap, cy + half + gap + pin_len),
        ):
            d.rounded_rectangle([cx + off - pin_thick // 2, y0, cx + off + pin_thick // 2, y1],
                                pin_thick // 2, fill=PIN + (255,))

    d.rounded_rectangle([cx - half, cy - half, cx + half, cy + half], rad,
                        fill=CHIP_FILL + (255,), outline=CHIP + (255,), width=stroke)

    head_len = int(S * 0.120)
    head_half = int(S * 0.098)
    shaft_half = int(S * 0.032)
    x0 = int(S * 0.058)
    d.rounded_rectangle([x0, cy - shaft_half, cx - head_len, cy + shaft_half],
                        shaft_half // 2, fill=ARROW + (255,))
    d.polygon([(cx - head_len, cy - head_half), (cx - head_len, cy + head_half),
               (cx, cy)], fill=ARROW + (255,))
    return img


def render_mesa(S):
    horizon = int(S * 0.795)
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # dusk sky: top -> mid -> horizon
    sky = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sky)
    for y in range(horizon):
        t = y / (horizon - 1)
        c = lerp(SKY_TOP, SKY_MID, t / 0.58) if t < 0.58 else lerp(SKY_MID, SKY_HORIZON, (t - 0.58) / 0.42)
        sd.line([(0, y), (S, y)], fill=c + (255,))
    gd = ImageDraw.Draw(sky)
    for y in range(horizon, S):
        t = (y - horizon) / max(1, S - horizon - 1)
        gd.line([(0, y), (S, y)], fill=lerp((30, 16, 30), (12, 7, 12), t) + (255,))
    sky.putalpha(rounded_mask(S, int(S * 0.22)))
    img.alpha_composite(sky)
    d = ImageDraw.Draw(img)

    # a few stars (only visible at large sizes, harmless when scaled down)
    for sx, sy, sr in ((0.70, 0.14, 0.006), (0.82, 0.24, 0.0045), (0.58, 0.09, 0.004)):
        d.ellipse([int(S * sx - S * sr), int(S * sy - S * sr),
                   int(S * sx + S * sr), int(S * sy + S * sr)],
                  fill=(255, 236, 210, 190))

    # back mesa (small, right, dimmer rim)
    bt, bl, br = int(S * 0.625), int(S * 0.615), int(S * 0.905)
    back = [(bl, bt), (br, bt), (int(S * 0.945), horizon), (int(S * 0.575), horizon)]
    d.polygon(back, fill=MESA_BACK + (255,))
    d.line([(bl, bt), (br, bt)], fill=(255, 186, 120, 120), width=max(3, int(S * 0.009)))

    # front mesa: flat top, slightly flared cliffs
    top = int(S * 0.545)
    l_top, r_top = int(S * 0.22), int(S * 0.575)
    l_base, r_base = int(S * 0.185), int(S * 0.615)
    front = [(l_top, top), (r_top, top), (r_base, horizon), (l_base, horizon)]
    d.polygon(front, fill=MESA_FRONT + (255,))
    d.line([(l_top, top), (r_top, top)], fill=(255, 194, 126, 215),
           width=max(4, int(S * 0.011)))

    # ring above the front mesa
    rc = (int(S * 0.405), int(S * 0.285))
    rr = int(S * 0.088)
    rw = max(4, int(S * 0.024))
    d.ellipse([rc[0] - rr, rc[1] - rr, rc[0] + rr, rc[1] + rr],
              outline=RING + (245,), width=rw)
    return img


def write_densities(img, prefix=""):
    for name, px in DENSITIES.items():
        out = os.path.join(RES, "mipmap-" + name)
        os.makedirs(out, exist_ok=True)
        img.resize((px, px), Image.LANCZOS).save(os.path.join(out, "ic_launcher.png"))
        print("wrote", os.path.join(out, "ic_launcher.png"), px)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    design = args[0] if args else os.environ.get("ICON_DESIGN", "chip")
    to_res = design == "chip" or "--res" in sys.argv

    img = render_mesa(SIZE) if design == "mesa" else render_chip(SIZE)
    os.makedirs(PREVIEW, exist_ok=True)
    suffix = "" if design == "chip" else "-C"
    img.save(os.path.join(PREVIEW, "icon%s-512.png" % suffix))
    img.resize((48, 48), Image.LANCZOS).save(os.path.join(PREVIEW, "icon%s-48.png" % suffix))

    if to_res:
        write_densities(img)
    else:
        print("preview only:", os.path.join(PREVIEW, "icon%s-512.png" % suffix))


if __name__ == "__main__":
    main()
