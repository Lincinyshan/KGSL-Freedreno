#!/usr/bin/env python3
"""Build a side-by-side preview of the two icon designs (not committed)."""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PREV = os.path.join(HERE, "preview")
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
BIG = 320
SMALL = 96
PAD = 24
LABEL_H = 34

b = Image.open(os.path.join(PREV, "icon-512.png")).convert("RGBA")
c = Image.open(os.path.join(PREV, "icon-C-512.png")).convert("RGBA")
b48 = Image.open(os.path.join(PREV, "icon-48.png")).convert("RGBA")
c48 = Image.open(os.path.join(PREV, "icon-C-48.png")).convert("RGBA")

W = PAD * 3 + BIG * 2
H = PAD * 2 + LABEL_H + BIG + PAD + SMALL + LABEL_H
sheet = Image.new("RGBA", (W, H), (24, 24, 28, 255))
d = ImageDraw.Draw(sheet)
font = ImageFont.truetype(FONT, 22)
small_font = ImageFont.truetype(FONT, 18)

def paste(img, x, y, size):
    sheet.alpha_composite(img.resize((size, size), Image.LANCZOS), (x, y))

x1, x2 = PAD, PAD * 2 + BIG
y0 = PAD
paste(b, x1, y0, BIG)
paste(c, x2, y0, BIG)
d.text((x1, y0 + BIG + 6), "B  chip + arrow", font=font, fill=(230, 230, 235, 255))
d.text((x2, y0 + BIG + 6), "C  mesa at dusk", font=font, fill=(230, 230, 235, 255))

y1 = y0 + BIG + LABEL_H
paste(b48, x1, y1, SMALL)
paste(c48, x2, y1, SMALL)
d.text((x1 + SMALL + 16, y1 + SMALL // 2 - 12), "48px  (launcher size)", font=small_font, fill=(160, 160, 170, 255))
d.text((x2 + SMALL + 16, y1 + SMALL // 2 - 12), "48px  (launcher size)", font=small_font, fill=(160, 160, 170, 255))

out = os.path.join(PREV, "compare.png")
sheet.convert("RGB").save(out)
print("wrote", out, sheet.size)
