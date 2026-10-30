#!/usr/bin/env python3
"""Adds Russian to assets/font.png: the 96 ASCII letters stay, the Cyrillic ones go below them (8x12 cells).
The letters are the thin shapes of tools/cyrillic.py, fattened the way the font's own stems are."""
import os
from PIL import Image
from cyrillic import UPPER, LOWER, LOWER_TALL, DENSE, letters

HERE = os.path.dirname(os.path.abspath(__file__))
PATH = os.path.join(HERE, "..", "assets", "font.png")
CW, CH = 8, 12
base = Image.open(PATH).convert("RGBA").crop((0, 0, 16 * CW, 6 * CH))
order = letters()
sheet = Image.new("RGBA", (16 * CW, (6 + (len(order) + 15) // 16) * CH), (0, 0, 0, 0))
sheet.paste(base, (0, 0))
px = sheet.load()

def stems(cx, cy, rows, top):
    for j, r in enumerate(rows):
        for i, b in enumerate(r):
            if b == "1":
                px[cx + 1 + i, cy + top + j] = px[cx + 2 + i, cy + top + j] = (255, 255, 255, 255)

def full(cx, cy, rows, top):
    for j, r in enumerate(rows):
        for i, b in enumerate(r):
            if b == "#":
                px[cx + i, cy + top + j] = (255, 255, 255, 255)

for n, ch in enumerate(order):
    cx, cy = n % 16 * CW, (6 + n // 16) * CH
    if ch in DENSE:
        top, rows = DENSE[ch]
        full(cx + 1 if len(rows.split()[0]) < 8 else cx, cy, rows.split(), top)
    elif ch in UPPER:
        rows = UPPER[ch].split()
        stems(cx, cy, rows[:3] + rows[3:4] * 2 + rows[4:], 2)     # the capitals of the font are 8 tall
    elif ch in LOWER:
        stems(cx, cy, LOWER[ch].split(), 4)
    elif ch == 'ё':
        stems(cx, cy, LOWER['е'].split(), 4)
        stems(cx, cy, ["01010"], 2)
    else:
        top, rows = LOWER_TALL[ch]
        stems(cx, cy, rows.split(), top)
sheet.save(PATH)
