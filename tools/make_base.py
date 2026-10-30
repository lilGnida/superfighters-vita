#!/usr/bin/env python3
"""The base: one building cut from the original game's level art, and the capitals used on its panel.
Writes assets/images/base/building.png (480x272, shown at 2x) and assets/images/base/caps.png.
Room floors and the ladder must match the constants at the top of src/base.cpp."""
import os
from PIL import Image, ImageDraw
from cyrillic import UPPER, letters

P = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..") + "/"
ART = P + "tools/levels/art/"
IMG = P + "assets/images/"
OUT = IMG + "base/"
W, H = 480, 272

wh = Image.open(ART + "warehouse.png").convert("RGBA")
rf = Image.open(ART + "rooftops.png").convert("RGBA")

BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
YELLOW = (255, 255, 0)
GREY = (150, 150, 150)
DARK = (70, 70, 70)
TEAL = (0, 128, 128)
GREEN = (0, 200, 0)

# ---------------------------------------------------------------- the original 5x7 capitals
F5 = {
 'A':"01110 10001 10001 11111 10001 10001 10001", 'B':"11110 10001 10001 11110 10001 10001 11110",
 'C':"01110 10001 10000 10000 10000 10001 01110", 'D':"11110 10001 10001 10001 10001 10001 11110",
 'E':"11111 10000 10000 11110 10000 10000 11111", 'F':"11111 10000 10000 11110 10000 10000 10000",
 'G':"01110 10001 10000 10111 10001 10001 01111", 'H':"10001 10001 10001 11111 10001 10001 10001",
 'I':"01110 00100 00100 00100 00100 00100 01110", 'J':"00111 00010 00010 00010 00010 10010 01100",
 'K':"10001 10010 10100 11000 10100 10010 10001", 'L':"10000 10000 10000 10000 10000 10000 11111",
 'M':"10001 11011 10101 10101 10001 10001 10001", 'N':"10001 11001 10101 10011 10001 10001 10001",
 'O':"01110 10001 10001 10001 10001 10001 01110", 'P':"11110 10001 10001 11110 10000 10000 10000",
 'Q':"01110 10001 10001 10001 10101 10010 01101", 'R':"11110 10001 10001 11110 10100 10010 10001",
 'S':"01111 10000 10000 01110 00001 00001 11110", 'T':"11111 00100 00100 00100 00100 00100 00100",
 'U':"10001 10001 10001 10001 10001 10001 01110", 'V':"10001 10001 10001 10001 10001 01010 00100",
 'W':"10001 10001 10001 10101 10101 11011 10001", 'X':"10001 10001 01010 00100 01010 10001 10001",
 'Y':"10001 10001 01010 00100 00100 00100 00100", 'Z':"11111 00001 00010 00100 01000 10000 11111",
 '0':"01110 10011 10101 10101 10101 11001 01110", '1':"00100 01100 00100 00100 00100 00100 01110",
 '2':"01110 10001 00001 00110 01000 10000 11111", '3':"11110 00001 00001 01110 00001 00001 11110",
 '4':"00010 00110 01010 10010 11111 00010 00010", '5':"11111 10000 11110 00001 00001 10001 01110",
 '6':"00110 01000 10000 11110 10001 10001 01110", '7':"11111 00001 00010 00100 01000 01000 01000",
 '8':"01110 10001 10001 01110 10001 10001 01110", '9':"01110 10001 10001 01111 00001 00010 01100",
 ':':"00000 00100 00100 00000 00100 00100 00000", '.':"00000 00000 00000 00000 00000 00100 00100",
 ',':"00000 00000 00000 00000 00100 00100 01000", '-':"00000 00000 00000 11111 00000 00000 00000",
 '+':"00000 00100 00100 11111 00100 00100 00000", '/':"00001 00010 00010 00100 01000 01000 10000",
 '!':"00100 00100 00100 00100 00100 00000 00100", "'":"00100 00100 00000 00000 00000 00000 00000",
 '<':"00010 00100 01000 10000 01000 00100 00010", '>':"01000 00100 00010 00001 00010 00100 01000",
 '%':"11001 11010 00010 00100 01000 01011 10011", ' ':"00000 "*7,
 '(':"00010 00100 01000 01000 01000 00100 00010", ')':"01000 00100 00010 00010 00010 00100 01000",
 '?':"01110 10001 00001 00110 00100 00000 00100", '*':"00000 10101 01110 11111 01110 10101 00000",
 '^':"00000 00100 01010 01010 10001 11111 00000", '=':"00000 00000 11111 00000 11111 00000 00000",
 '&':"01100 10010 10100 01000 10101 10010 01101", ';':"00000 00100 00100 00000 00100 00100 01000",
 '"':"01010 01010 00000 00000 00000 00000 00000", '#':"01010 01010 11111 01010 11111 01010 01010",
}
# ---------------------------------------------------------------- pieces cut from the levels
def cut(src, box): return src.crop(box)

def recolor(chunk, hue):
    """the art uses one family of shades per room: swap teal for another family"""
    fams = {
        "teal": ((0, 64, 64), (0, 128, 128), (0, 200, 200)),
        "blue": ((0, 0, 90), (0, 0, 170), (40, 60, 240)),
        "red": ((110, 0, 0), (210, 0, 0), (255, 60, 40)),
        "orange": ((130, 50, 0), (235, 120, 0), (255, 190, 40)),
        "green": ((0, 80, 0), (0, 150, 0), (70, 230, 70)),
        "purple": ((60, 0, 110), (120, 40, 190), (180, 110, 240)),
    }[hue]
    out = chunk.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if a == 0 or (r, g, b) == BLACK:
                continue
            spread = max(r, g, b) - min(r, g, b)
            if spread < 24:      # greys and whites stay
                continue
            l = max(r, g, b)
            f = fams[0] if l < 95 else (fams[1] if l < 190 else fams[2])
            px[x, y] = f + (a,)
    return out

def tile_x(strip, w):
    out = Image.new("RGBA", (w, strip.height))
    for x in range(0, w, strip.width):
        out.paste(strip, (x, 0))
    return out

def tile_y(strip, h):
    out = Image.new("RGBA", (strip.width, h))
    for y in range(0, h, strip.height):
        out.paste(strip, (0, y))
    return out

SLAB = cut(wh, (120, 39, 140, 50))
SLAB_V = cut(wh, (339, 60, 350, 80))
BRICK = cut(wh, (35, 60, 52, 84))
WINDOW = cut(wh, (60, 65, 110, 89))
EXIT = cut(wh, (466, 110, 476, 118))
LOCKER2 = cut(wh, (112, 86, 140, 110))

BACK = {
    "office": cut(wh, (341, 78, 466, 141)),        # vents and arches
    "blue": cut(rf, (552, 182, 710, 258)),
    "red": cut(rf, (565, 295, 705, 366)),
    "gates": cut(rf, (717, 330, 848, 383)),
}

def scrub(chunk, test):
    out = chunk.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            if test(*px[x, y][:3]):
                px[x, y] = (0, 0, 0, 255)
    return out

BACK["red"] = scrub(BACK["red"], lambda r, g, b: max(r, g, b) - min(r, g, b) > 24 and r < 170 and g > 20)
BACK["gates"] = scrub(BACK["gates"], lambda r, g, b: max(r, g, b) - min(r, g, b) < 24 and r > 60)

def put(im, sprite, x, y):
    im.paste(sprite.convert("RGB"), (x, y), sprite)

def slab(im, x0, y0, x1):
    s = tile_x(SLAB, x1 - x0)
    put(im, s, x0, y0)
    d = ImageDraw.Draw(im)
    d.rectangle([x0, y0, x0, y0 + 10], fill=(90, 90, 90))
    d.rectangle([x1 - 1, y0, x1 - 1, y0 + 10], fill=(90, 90, 90))

def post(im, x, y0, y1):
    put(im, tile_y(SLAB_V, y1 - y0), x, y0)

def brick(im, x, y0, y1):
    put(im, tile_y(BRICK, y1 - y0), x, y0)

def with_halo(im, draw):
    """things in front of a busy wall get a black edge, so they read"""
    l = Image.new("RGBA", im.size, (0, 0, 0, 0))
    draw(l)
    a = l.getchannel("A")
    edge = Image.new("L", im.size, 0)
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
        edge.paste(255, (dx, dy), a)
    im.paste(BLACK, (0, 0), edge)
    im.paste(l.convert("RGB"), (0, 0), a)

def sprite(name, frame=None):
    s = Image.open(IMG + name).convert("RGBA")
    if frame:
        s = s.crop(frame)
    return s

def rack(im, x, y, w, h, items):
    """weapons on a wall: thin rails, black ground, the game's own sprites"""
    d = ImageDraw.Draw(im)
    d.rectangle([x, y, x + w, y + h], fill=(62, 62, 62), outline=(170, 170, 170))
    for i in range(1, 3):
        d.line([x + 1, y + i * h // 3, x + w - 1, y + i * h // 3], fill=(90, 90, 90))
    cx, cy = x + 3, y + 3
    for name in items:
        s = Image.open(IMG + "weapon/%s.png" % name).convert("RGBA")
        s = s.crop(s.getbbox())
        if cx + s.width > x + w - 2:
            cx, cy = x + 3, cy + h // 3
        put(im, s, cx, cy)
        cx += s.width + 3

def barbell(im, x, y):
    d = ImageDraw.Draw(im)
    d.line([x, y, x + 34, y], fill=(200, 200, 200))
    for dx in (3, 6, 28, 31):
        d.rectangle([x + dx, y - 5, x + dx + 2, y + 5], fill=(30, 30, 30), outline=(200, 200, 200))

def bag(im, x, top, floor):
    d = ImageDraw.Draw(im)
    d.line([x + 4, top, x + 4, top + 8], fill=(200, 200, 200))
    d.rounded_rectangle([x, top + 8, x + 9, top + 34], 2, fill=(150, 95, 45), outline=BLACK)
    d.line([x + 2, top + 10, x + 2, top + 31], fill=(205, 150, 90))
    d.line([x, top + 16, x + 9, top + 16], fill=BLACK)
    d.line([x, top + 26, x + 9, top + 26], fill=BLACK)

def bench(im, x, floor):
    d = ImageDraw.Draw(im)
    d.rectangle([x, floor - 8, x + 28, floor - 5], fill=(235, 235, 235), outline=BLACK)
    d.rectangle([x + 3, floor - 5, x + 5, floor - 1], fill=(150, 150, 150), outline=BLACK)
    d.rectangle([x + 23, floor - 5, x + 25, floor - 1], fill=(150, 150, 150), outline=BLACK)
    d.line([x + 25, floor - 7, x + 25, floor - 20], fill=(200, 200, 200))
    d.line([x + 15, floor - 20, x + 35, floor - 20], fill=(200, 200, 200))

def treadmill(im, x, floor):
    d = ImageDraw.Draw(im)
    d.rectangle([x, floor - 5, x + 34, floor - 2], fill=(45, 45, 45), outline=(200, 200, 200))
    d.line([x + 2, floor - 1, x + 32, floor - 1], fill=(120, 120, 120))
    d.line([x + 33, floor - 5, x + 37, floor - 24], fill=(200, 200, 200))
    d.rectangle([x + 34, floor - 27, x + 40, floor - 23], fill=(30, 30, 30), outline=(200, 200, 200))

def board(im, x, y):
    """the contracts, pinned up"""
    d = ImageDraw.Draw(im)
    d.rectangle([x, y, x + 26, y + 20], fill=(150, 95, 45), outline=(200, 200, 200))
    for i, (px, py) in enumerate(((3, 3), (11, 2), (19, 4), (5, 12), (14, 11))):
        d.rectangle([px + x, py + y, px + x + 5, py + y + 6], fill=(235, 235, 235))
        d.point([px + x + 2, py + y], fill=(210, 0, 0))

# ---------------------------------------------------------------- rooms
def backdrop(im, chunk, x, floor, w, maxh):
    h = min(maxh, chunk.height)
    put(im, chunk.crop((0, chunk.height - h, min(w, chunk.width), chunk.height)), x, floor - h)

def room_wardrobe(im, x, floor, w, maxh, hue="teal"):
    backdrop(im, recolor(BACK["office"], hue), x, floor, w, maxh)
    put(im, recolor(LOCKER2, hue), x + 8, floor - LOCKER2.height)
    put(im, recolor(LOCKER2, hue), x + 37, floor - LOCKER2.height)
    # coat rail
    d = ImageDraw.Draw(im)
    d.line([x + 72, floor - 34, x + 112, floor - 34], fill=(200, 200, 200))
    d.line([x + 74, floor - 34, x + 74, floor - 1], fill=(200, 200, 200))
    d.line([x + 110, floor - 34, x + 110, floor - 1], fill=(200, 200, 200))
    for i, c in enumerate(((60, 60, 70), (170, 30, 20), (30, 90, 60), (230, 230, 230), (40, 50, 130))):
        cx = x + 79 + i * 6
        d.rectangle([cx, floor - 32, cx + 4, floor - 12], fill=c, outline=BLACK)

def room_armory(im, x, floor, w, maxh):
    backdrop(im, BACK["blue"], x, floor, w, maxh)
    top = floor - min(60, maxh - 4)
    rack(im, x + 6, top, 66, 30, ["pistol", "uzi", "magnum", "shotgun", "rifle", "sniper"])
    if w > 112:
        put(im, sprite("desk.png"), x + 74, floor - 14)
        put(im, sprite("computer_monitor.png"), x + 92, floor - 26)
    put(im, sprite("filecab.png"), x + 8, floor - 15)
    put(im, sprite("crate.png"), x + 26, floor - 16)
    put(im, sprite("crate.png"), x + 43, floor - 16)

def room_gym(im, x, floor, w, maxh):
    backdrop(im, BACK["red"], x, floor, w, maxh)
    with_halo(im, lambda l: (bag(l, x + 14, floor - min(52, maxh), floor), bench(l, x + 40, floor), barbell(l, x + 84, floor - 7),
                             treadmill(l, x + 148, floor)))

def room_range(im, x, floor, w, maxh):
    backdrop(im, BACK["gates"], x, floor, w, maxh)
    t = sprite("target.png")
    for i in range(3):
        put(im, t, x + w - 16 - i * 30, floor - 16)
    put(im, sprite("crate.png"), x + 10, floor - 16)


# ---------------------------------------------------------------- the building
X0, X1 = 10, 292
ROOMS_X = X0 + 62
FLOORS = [64, 126, 188, 250]
LADDER_X = X0 + 23

def building():
    im = Image.new("RGB", (W, H), BLACK)
    brick(im, X0, 0, H)
    brick(im, X1 - 2, 0, H)
    slab(im, X0 + 16, 3, X1 - 2)
    x = ROOMS_X
    for f, fn, w in zip(FLOORS, (room_wardrobe, room_armory, room_gym, room_range), (128, 158, 190, 131)):
        fn(im, x, f, w, 48)
        slab(im, X0 + 16, f, X1 - 2)
    d = ImageDraw.Draw(im)
    # the shaft: one ladder through all the floors
    for f in FLOORS[:-1]:
        d.rectangle([X0 + 21, f, X0 + 36, f + 10], fill=BLACK)
    ladder = cut(wh, (54, 95, 66, 175))
    for yy in range(14, FLOORS[-1], ladder.height):
        put(im, ladder.crop((0, 0, 12, min(ladder.height, FLOORS[-1] - yy))), LADDER_X, yy)
    for f in FLOORS:
        put(im, EXIT, X0 + 42, f - 22)
    put(im, WINDOW, x + 164, 28)
    board(im, x + 132, 30)
    put(im, sprite("explosive_barrel.png").crop((0, 0, 11, 15)), x + 150, 235)
    put(im, sprite("crate.png"), x + 170, 234)
    # the work panel's frame
    post(im, 299, 0, H)
    slab(im, 299, 0, W)
    slab(im, 299, H - 11, W)
    return im

def caps():
    """the slots of the main font (see glyphIndex in platform.cpp) in 6x8 cells, white on nothing; the code tints them"""
    cyr = letters()
    im = Image.new("RGBA", (16 * 6, (6 + (len(cyr) + 15) // 16) * 8), (0, 0, 0, 0))
    px = im.load()
    def letter(slot, rows):
        cx, cy = slot % 16 * 6, slot // 16 * 8
        for j, r in enumerate(rows):
            for i, b in enumerate(r):
                if b == "1":
                    px[cx + i, cy + j] = (255, 255, 255, 255)
    for c in range(32, 96):
        letter(c - 32, F5.get(chr(c), F5[" "]).split())
    for n, ch in enumerate(cyr):
        if ch in UPPER:
            letter(96 + n, UPPER[ch].split())
    return im

os.makedirs(OUT, exist_ok=True)
building().save(OUT + "building.png")
caps().save(OUT + "caps.png")
