#!/usr/bin/env python3
"""Generate the FCL renderer-plugin icon.

Design "B": a GPU die outline with pins, and a white arrow piercing in from
the left, tip stopping at the die centre (direct-to-GPU / zero-copy).

Renders at 512 and downsamples to the five mipmap densities.
Run from anywhere:  python3 make-icon.py
"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "res")
PREVIEW = os.path.join(HERE, "preview")
SIZE = 512
DENSITIES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}

BG_TOP = (18, 22, 28)
BG_BOTTOM = (9, 12, 16)
FRAME = (42, 51, 63)
CHIP = (245, 166, 35)          # amber die
CHIP_FILL = (26, 18, 7)        # very dark amber, keeps the die readable
PIN = (90, 102, 117)
ARROW = (245, 247, 250)


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size - 1, size - 1], radius, fill=255)
    return m


def gradient(size):
    grad = Image.new("RGBA", (size, size))
    gd = ImageDraw.Draw(grad)
    for y in range(size):
        t = y / (size - 1)
        c = tuple(int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t) for i in range(3))
        gd.line([(0, y), (size, y)], fill=c + (255,))
    grad.putalpha(rounded_mask(size, int(size * 0.22)))
    return grad


def main():
    S = SIZE
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    img.alpha_composite(gradient(S))
    d = ImageDraw.Draw(img)

    d.rounded_rectangle([10, 10, S - 11, S - 11], int(S * 0.20),
                        outline=FRAME + (200,), width=4)

    cx, cy = S // 2, S // 2
    half = int(S * 0.205)
    stroke = int(S * 0.035)
    rad = int(S * 0.10)

    # pins (behind the die)
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

    # die: dark fill + amber outline
    d.rounded_rectangle([cx - half, cy - half, cx + half, cy + half], rad,
                        fill=CHIP_FILL + (255,), outline=CHIP + (255,), width=stroke)

    # arrow: shaft from the left edge, head with the tip at the die centre
    head_len = int(S * 0.120)
    head_half = int(S * 0.098)
    shaft_half = int(S * 0.032)
    x0 = int(S * 0.058)
    d.rounded_rectangle([x0, cy - shaft_half, cx - head_len, cy + shaft_half],
                        shaft_half // 2, fill=ARROW + (255,))
    d.polygon([(cx - head_len, cy - head_half), (cx - head_len, cy + head_half),
               (cx, cy)], fill=ARROW + (255,))

    os.makedirs(PREVIEW, exist_ok=True)
    img.save(os.path.join(PREVIEW, "icon-512.png"))

    for name, px in DENSITIES.items():
        out = os.path.join(RES, "mipmap-" + name)
        os.makedirs(out, exist_ok=True)
        img.resize((px, px), Image.LANCZOS).save(os.path.join(out, "ic_launcher.png"))
        print("wrote", os.path.join(out, "ic_launcher.png"), px)


if __name__ == "__main__":
    main()
