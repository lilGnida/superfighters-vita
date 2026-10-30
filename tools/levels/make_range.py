#!/usr/bin/env python3
"""The picture of the shooting range: the left hall of the warehouse, closed with a wall of its own, the
warehouse's door moved to the entrance, and the rails the moving targets run on.
Writes tools/levels/art/range.png; the coordinates are the ones of LEVELS["range"] in build_levels.py."""
import os
from PIL import Image, ImageDraw

ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "art")
wh = Image.open(os.path.join(ART, "warehouse.png")).convert("RGB")
im = Image.new("RGB", (374, 328), (0, 0, 0))
im.paste(wh.crop((0, 0, 350, 328)), (0, 0))

# the wall on the right is the left one, mirrored
wall = wh.crop((30, 30, 53, 292)).transpose(Image.FLIP_LEFT_RIGHT)
im.paste(wall, (350, 30))

# the way out
door = wh.crop((445, 210, 466, 255)).transpose(Image.FLIP_LEFT_RIGHT)
im.paste(door, (54, 210))
im.paste(wh.crop((466, 110, 476, 118)), (58, 198))

d = ImageDraw.Draw(im)
def rail(x0, y0, x1, y1):
    d.line([x0, y0, x1, y1], fill=(190, 190, 190), width=1)
    if y0 == y1:
        d.line([x0, y0 + 1, x1, y1 + 1], fill=(90, 90, 90))
        for x in (x0, x1):
            d.rectangle([x - 1, y0 - 2, x + 1, y0 + 3], fill=(255, 210, 0), outline=(0, 0, 0))
    else:
        d.line([x0 + 1, y0, x1 + 1, y1], fill=(90, 90, 90))
        for y in (y0, y1):
            d.rectangle([x0 - 2, y - 1, x0 + 3, y + 1], fill=(255, 210, 0), outline=(0, 0, 0))

rail(146, 91, 324, 91)
rail(66, 207, 150, 207)
rail(226 - 4, 169, 300, 169)
rail(344, 114, 344, 230)

im.save(os.path.join(ART, "range.png"))
