#!/usr/bin/env python3
"""Generate the FCL renderer-plugin icon: "GL4.6" with a small "NATIVE" badge.

Renders once at high resolution, then downsamples to the five mipmap densities.
Run from anywhere:  python3 make-icon.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "res")
SIZE = 512
DENSITIES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}

FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FONT_MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

BG_TOP = (16, 34, 22)
BG_BOTTOM = (5, 14, 9)
ACCENT = (255, 167, 38)
WHITE = (255, 255, 255)


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size - 1, size - 1], radius, fill=255)
    return m


def main():
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    # vertical gradient background
    grad = Image.new("RGBA", (SIZE, SIZE))
    gd = ImageDraw.Draw(grad)
    for y in range(SIZE):
        t = y / (SIZE - 1)
        c = tuple(int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t) for i in range(3))
        gd.line([(0, y), (SIZE, y)], fill=c + (255,))
    grad.putalpha(rounded_mask(SIZE, int(SIZE * 0.22)))
    img.alpha_composite(grad)

    d = ImageDraw.Draw(img)

    # thin accent frame
    d.rounded_rectangle([8, 8, SIZE - 9, SIZE - 9], int(SIZE * 0.20),
                        outline=ACCENT + (90,), width=3)

    # "GL4.6" — main text
    main_font = ImageFont.truetype(FONT_BOLD, int(SIZE * 0.255))
    text = "GL4.6"
    bbox = d.textbbox((0, 0), text, font=main_font)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    cx, cy = SIZE // 2, SIZE // 2 - 26
    d.text((cx - w / 2 - bbox[0], cy - h / 2 - bbox[1]), text, font=main_font, fill=WHITE)

    # accent underline
    bar_w, bar_h = int(w * 0.92), 8
    d.rounded_rectangle([cx - bar_w // 2, cy + h // 2 + 26,
                         cx + bar_w // 2, cy + h // 2 + 26 + bar_h],
                        bar_h // 2, fill=ACCENT + (255,))

    # "NATIVE" badge, bottom right
    small = ImageFont.truetype(FONT_MONO, int(SIZE * 0.105))
    label = "NATIVE"
    spaced = " ".join(label)
    b2 = d.textbbox((0, 0), spaced, font=small)
    sw, sh = b2[2] - b2[0], b2[3] - b2[1]
    pad = 10
    x1, y1 = SIZE - 30 - sw - pad, SIZE - 30 - sh - pad
    d.rounded_rectangle([x1 - pad, y1 - pad + 2, x1 + sw + pad, y1 + sh + pad],
                        radius=int(SIZE * 0.045), fill=ACCENT + (255,))
    d.text((x1 - b2[0], y1 - b2[1]), spaced, font=small, fill=(20, 20, 20, 255))

    for density, px in DENSITIES.items():
        out_dir = os.path.join(RES, "mipmap-" + density)
        os.makedirs(out_dir, exist_ok=True)
        thumb = img.resize((px, px), Image.LANCZOS)
        path = os.path.join(out_dir, "ic_launcher.png")
        thumb.save(path, optimize=True)
        print("%-18s %4dx%-4d %s" % (os.path.relpath(path, HERE), px, px,
                                    "%d bytes" % os.path.getsize(path)))

    os.makedirs(os.path.join(HERE, "preview"), exist_ok=True)
    img.save(os.path.join(HERE, "preview", "icon-512.png"), optimize=True)
    print("preview:", os.path.join(HERE, "preview", "icon-512.png"))


if __name__ == "__main__":
    main()
