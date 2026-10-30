#!/usr/bin/env python3
"""The tiles Superfighters Deluxe maps are drawn with here (their own art is not ours to ship): 8x8 cells cut from
the level art. Row 0 solid materials with a lit top in row 1, row 2 the walls behind. Writes assets/images/sfd_tiles.png;
the slots are the Tile enum of src/sfdm.cpp."""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "levels", "art")
wh = Image.open(os.path.join(ART, "warehouse.png")).convert("RGB")
sheet = Image.new("RGBA", (64, 40), (0, 0, 0, 0))
d = ImageDraw.Draw(sheet)

def shade(c, f):
    return tuple(min(255, int(v * f)) for v in c)

def block(slot, row, c, top):
    x, y = slot * 8, row * 8
    d.rectangle([x, y, x + 7, y + 7], fill=c)
    d.line([x, y + 7, x + 7, y + 7], fill=shade(c, 0.72))
    d.line([x + 7, y, x + 7, y + 7], fill=shade(c, 0.8))
    if top:
        d.rectangle([x, y, x + 7, y + 1], fill=shade(c, 1.45))

# concrete, metal, stone, wood, dirt, asphalt, brick, roof
for slot, c in enumerate(((165, 165, 165), (112, 122, 138), (128, 122, 116), (150, 96, 52),
                          (112, 76, 46), (72, 72, 78), (160, 60, 44), (90, 96, 104))):
    block(slot, 0, c, False)
    block(slot, 1, c, True)

# the wall behind: teal brick of the warehouse and the same recoloured
brick = wh.crop((60, 190, 68, 198))
def tint(c):
    out = brick.convert("L").point(lambda v: min(255, v * 2)).convert("RGB")
    px = out.load()
    for y in range(8):
        for x in range(8):
            v = px[x, y][0] / 255
            px[x, y] = tuple(int(q * (0.3 + 0.7 * v)) for q in c)
    return out
for slot, c in enumerate(((0, 120, 120), (0, 80, 140), (120, 70, 30), (100, 86, 76), (60, 100, 50), (70, 70, 84))):
    sheet.paste(tint(c), (slot * 8, 16))

# barred window, ledge, ladder
d.rectangle([48, 16, 55, 23], fill=(0, 0, 0))
for x in (49, 52, 55):
    d.line([x, 16, x, 23], fill=(235, 235, 235))
d.rectangle([56, 16, 63, 18], fill=(235, 235, 235))
d.line([56, 19, 63, 19], fill=(90, 90, 90))
d.line([0, 24, 0, 31], fill=(200, 200, 200))
d.line([7, 24, 7, 31], fill=(200, 200, 200))
for y in (25, 29):
    d.line([0, y, 7, y], fill=(170, 170, 170))
sheet.save(os.path.join(HERE, "..", "assets", "images", "sfd_tiles.png"))
