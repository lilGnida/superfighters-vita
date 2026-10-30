#!/usr/bin/env python3
"""Builds playable maps from the original Superfighters level art.

Each level = the original bitmap drawn as a background layer + invisible collision
traced over it (coordinates are image pixels, the art is placed at world (0,0)).

  solid  : rectangles (x0, y0, x1, y1) or convex polygons [(x, y), ...]  -> "wall"
  ledges : one-way platforms (x0, x1, y_top)                              -> "ledge"
  ladders: (x0, x1, y_top, y_bottom)                                      -> "climb"
  spawns : player spawn points (x, y_of_the_floor)
  weapons: weapon drop points (x, y_of_the_floor)
  props  : interactive objects (key, x, y_of_the_floor)
  hanging: things on a chain (key, x, y_of_the_ceiling, chain_length)
  belts  : conveyors (x0, x1, y_of_the_floor, speed)

run: python3 tools/levels/build_levels.py   (writes assets/maps/*.sfm, copies art to assets/images/levels)
"""
import json, os, shutil

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
ART = os.path.join(HERE, "art")
ASSETS = os.path.join(ROOT, "assets")


def centroid(pts):
    a = cx = cy = 0.0
    n = len(pts)
    for i in range(n):
        x0, y0 = pts[i]
        x1, y1 = pts[(i + 1) % n]
        c = x0 * y1 - x1 * y0
        a += c
        cx += (x0 + x1) * c
        cy += (y0 + y1) * c
    a *= 0.5
    return cx / (6 * a), cy / (6 * a)


class Map:
    def __init__(self):
        self.objects = []

    def add(self, key, x, y, **extra):
        o = {"parentKey": key, "id": len(self.objects),
             "matter": {"position": {"x": round(x, 2), "y": round(y, 2)}}}
        o.update(extra)
        self.objects.append(o)

    def rect(self, key, x0, y0, x1, y1):
        self.add(key, (x0 + x1) / 2, (y0 + y1) / 2, size={"w": x1 - x0, "h": y1 - y0}, hidden=True)

    def poly(self, key, pts):
        cx, cy = centroid(pts)
        self.add(key, cx, cy, verts=[[x - cx, y - cy] for x, y in pts], hidden=True)


def build(name, level):
    m = Map()
    for s in level["solid"]:
        if len(s) == 4 and not isinstance(s[0], (list, tuple)):
            m.rect("wall", *s)
        else:
            m.poly("wall", s)
    for x0, x1, y in level.get("ledges", []):
        m.rect("ledge", x0, y, x1 + 1, y + 3)
    for x0, x1, y0, y1 in level.get("ladders", []):
        m.rect("climb", x0, y0, x1 + 1, y1)
    for x, y in level.get("spawns", []):
        m.add("player_spawn", x, y - 10)
    for x, y in level.get("weapons", []):
        m.add("weapon_spawn", x, y - 8)
    for key, x, y in level.get("props", []):
        m.add(key, x, y)
    for key, x, y, length in level.get("hanging", []):
        anchor = len(m.objects)
        m.add("anchor", x, y, hidden=True)
        m.add(key, x, y + length)
        m.add("distance_marker", x, y, targetAId=anchor, targetBId=anchor + 1)
    for x0, x1, y, speed in level.get("belts", []):
        m.add("conveyor", (x0 + x1) / 2, y + 1.5, size={"w": x1 - x0, "h": 3}, speed=speed)
    for t in level.get("targets", []):
        extra = {"rail": {"dx": t[2], "dy": t[3], "period": t[4]}} if len(t) > 2 else {}
        m.add("range_target", t[0], t[1], **extra)
    for x, y0, y1 in level.get("glass", []):
        n = max(1, round((y1 - y0) / 10))     # pane = n tiles of 3x10, standing on y1
        m.add("glass", x, y1 - 5 * n, tiling={"width": 1, "height": n})
    for x, y in level.get("exit", []):
        m.add("exit_door", x, y - 9.5, size={"w": 12, "h": 19}, hidden=True, customId="exit", link="__exit")
    for i, ((xa, fa), (xb, fb)) in enumerate(level.get("doors", [])):
        a, b = f"door{i}a", f"door{i}b"
        m.add("exit_door", xa, fa - 9.5, size={"w": 12, "h": 19}, hidden=True, customId=a, link=b)
        m.add("exit_door", xb, fb - 9.5, size={"w": 12, "h": 19}, hidden=True, customId=b, link=a)

    art = level["art"]
    os.makedirs(os.path.join(ASSETS, "images", "levels"), exist_ok=True)
    shutil.copy(os.path.join(ART, art), os.path.join(ASSETS, "images", "levels", art))
    if "pool_ladders" in level:
        paint_ladders(os.path.join(ASSETS, "images", "levels", art), level["pool_ladders"], level["acid"][2])
    layers = []
    for sky in level.get("sky", []):
        src = os.path.join(ART, os.path.basename(sky["image"]))
        shutil.copy(src, os.path.join(ASSETS, "images", "levels", os.path.basename(sky["image"])))
        layers.append(sky)
    layers.append({"image": "images/levels/" + art, "x": 0, "y": 0})

    x0, y0, x1, y1 = level["bounds"]
    data = {"name": level["title"], "layers": layers,
            "bounds": {"x": x0, "y": y0, "w": x1 - x0, "h": y1 - y0},
            "objects": m.objects}
    if "color" in level:
        data["color"] = level["color"]
    if "zoom" in level:
        data["zoom"] = level["zoom"]
    if "acid" in level:
        data["acid"] = list(level["acid"])
    if "deathY" in level:
        data["deathY"] = level["deathY"]
    out = os.path.join(ASSETS, "maps", name + ".sfm")
    json.dump(data, open(out, "w"), indent=1)
    print(f"{name}: {len(m.objects)} objects -> {os.path.relpath(out, ROOT)}")


# --------------------------------------------------------------------------- levels

def paint_ladders(path, ladders, surface):
    """Steps and ladders added to the bitmap: the way out of an acid pool."""
    im = Image.open(path).convert("RGBA")
    d = ImageDraw.Draw(im)
    for x0, x1, top in ladders:
        d.rectangle([x0 - 2, top, x1 + 2, top + 3], fill=(190, 190, 190, 255), outline=(30, 30, 30, 255))
        d.line([x0, top + 3, x0, surface], fill=(210, 210, 210, 255), width=2)
        d.line([x1 - 1, top + 3, x1 - 1, surface], fill=(210, 210, 210, 255), width=2)
        for y in range(top + 7, surface, 5):
            d.line([x0, y, x1, y], fill=(210, 210, 210, 255))
    im.save(path)


LEVELS = {}

# Shooting range of the base: the left hall of the warehouse (tools/levels/make_range.py adds the rails),
# the door on the left leads out. "targets" are (x, y) or (x, y, dx, dy, period): shuttles along a rail.
LEVELS["range"] = {
    "title": "Range",
    "art": "range.png",
    "bounds": (30, 30, 374, 292),
    "zoom": 3.0,
    "solid": [
        (30, 39, 53, 292),                # left brick wall
        (350, 30, 374, 292),              # right brick wall
        (53, 39, 350, 51),                # ceiling
        (339, 51, 350, 104),              # stub of the warehouse's divider
        [(53, 255), (133, 255), (154, 273), (154, 292), (53, 292)],     # left floor + ramp
        (154, 273, 304, 292),             # pit floor
        [(303, 273), (324, 255), (350, 255), (350, 292), (303, 292)],   # ramp + right floor
        [(152, 183), (156, 183), (179, 207), (175, 207)],     # stairs from the left gallery
    ],
    "ledges": [
        (68, 138, 111),                   # red girder
        (53, 153, 183),                   # left gallery
        (172, 219, 166),                  # small shelf
        (195, 296, 144),                  # middle catwalk
        (175, 265, 207), (175, 265, 229), (175, 265, 251),   # racks
    ],
    "ladders": [(54, 64, 95, 183)],
    "exit": [(63, 255)],
    "spawns": [(96, 255)],
    "weapons": [(120, 183), (250, 144), (205, 273), (112, 255), (330, 255), (240, 207)],
    "props": [("crate", 128, 247), ("explosive_barrel", 290, 265), ("crate", 336, 247)],
    "hanging": [("lamp", 120, 51, 24), ("lamp", 300, 51, 30)],
    "glass": [(283, 104, 144)],
    "targets": [(132, 100), (212, 154), (290, 132), (260, 196), (342, 243),
                (150, 100, 170, 0, 420), (70, 216, 76, 0, 300), (332, 120, 0, 104, 360), (226, 178, 70, 0, 240)],
}

# Warehouse: two floors, a pit with storage racks, upper office on the right.
LEVELS["warehouse"] = {
    "title": "Warehouse",
    "art": "warehouse.png",          # original bitmap #102 (522x328)
    "bounds": (30, 30, 490, 292),
    "solid": [
        (30, 39, 53, 292),                # left brick wall
        (466, 69, 490, 292),              # right brick wall
        (53, 39, 350, 51),                # ceiling
        (339, 51, 350, 104),              # divider above the office
        (350, 69, 466, 80),               # office ceiling
        (339, 141, 466, 152),             # office floor
        # floors joined with the ramps (no seam where the ramp meets the floor above it)
        [(53, 255), (133, 255), (154, 273), (154, 292), (53, 292)],     # left floor + ramp
        (154, 273, 304, 292),             # pit floor
        [(303, 273), (324, 255), (466, 255), (466, 292), (303, 292)],   # ramp + right floor
        [(152, 183), (156, 183), (179, 207), (175, 207)],     # stairs from the left gallery
    ],
    "ledges": [
        (68, 138, 111),                   # red girder
        (53, 153, 183),                   # left gallery
        (172, 219, 166),                  # small shelf
        (195, 296, 144),                  # middle catwalk
        (379, 465, 189),                  # right shelf
        (175, 265, 207), (175, 265, 229), (175, 265, 251),   # left racks
        (346, 436, 211), (346, 436, 233),                    # right racks
    ],
    "ladders": [(54, 64, 95, 183)],
    "doors": [((458, 141), (458, 189))],
    "spawns": [(90, 183), (100, 111), (245, 144), (400, 141), (90, 255), (230, 273), (420, 255), (420, 189)],
    "weapons": [(120, 183), (260, 144), (440, 141), (230, 273), (390, 255), (300, 144)],
    "props": [("crate", 290, 265), ("crate", 440, 247), ("explosive_barrel", 70, 247),
              ("desk", 385, 134), ("computer_monitor", 380, 121), ("chair", 405, 132.5),
              ("filecab", 362, 133.5), ("table", 100, 250), ("small_table", 118, 251.5),
              ("crate", 220, 136), ("crate", 340, 247), ("crate", 340, 231)],
    "glass": [(344, 104, 141)],
    "hanging": [("crate_hanging", 250, 51, 42), ("lamp", 120, 51, 24), ("lamp", 300, 51, 30), ("lamp", 405, 80, 18)],
}

# Backstreets: two buildings, a fenced dirt pit between them, night city behind.
LEVELS["backstreets"] = {
    "title": "Backstreets",
    "art": "backstreets.png",        # original bitmap #2231 (589x240)
    "bounds": (28, 14, 582, 232),
    "hanging": [("lamp", 130, 37, 24), ("lamp", 490, 47, 30)],
    "solid": [
        # left building
        (28, 25, 53, 133),                # upper brick wall
        (28, 133, 71, 240),               # lower brick wall
        (53, 25, 206, 37),                # roof
        (195, 37, 206, 85),               # upper outer wall (window below)
        (53, 121, 206, 133),              # upper floor
        (195, 133, 206, 155),             # lower outer wall
        (71, 209, 100, 240),              # bottom-left floor
        # stairs + lower floor + slope into the pit, one piece
        [(99, 209), (120, 191), (206, 191), (246, 210), (246, 240), (99, 240)],
        (246, 210, 288, 240),             # pit bottom
        [(287, 210), (327, 191), (419, 191), (419, 240), (287, 240)],   # slope + floor
        # right building
        (328, 77, 339, 99),               # outer wall stub
        (339, 77, 431, 89),               # low roof
        (432, 35, 557, 47),               # high roof
        (432, 47, 443, 89),               # inner wall stub
        (419, 179, 557, 240),             # raised floor
        (556, 35, 582, 240),              # right brick wall
    ],
    "ledges": [
        (328, 372, 138), (386, 461, 138),  # walkway over the machines
        (513, 548, 110),                   # red girder
    ],
    "ladders": [(374, 384, 117, 191), (507, 517, 90, 179)],
    "doors": [((60, 121), (80, 209))],
    "spawns": [(100, 121), (170, 121), (150, 191), (85, 209), (266, 210), (350, 191),
               (450, 179), (420, 138), (530, 110)],
    "weapons": [(130, 121), (266, 210), (400, 191), (480, 179), (350, 138)],
    "props": [("crate", 165, 183), ("explosive_barrel", 400, 183.5), ("crate", 540, 171),
              ("desk", 110, 114), ("computer_monitor", 104, 101), ("computer_desktop", 115, 101),
              ("chair", 146, 112.5), ("filecab", 180, 113.5), ("table", 470, 174), ("small_table", 500, 175.5)],
    "glass": [(200, 85, 121), (200, 155, 191), (333, 99, 138)],
}

# Factory: long hall, raised middle section with storage racks, galleries on both sides.
LEVELS["factory"] = {
    "title": "Factory",
    "art": "factory.png",            # original bitmap #2233 (637x273), roof shafts blacked out
    "bounds": (16, 62, 622, 266),
    "solid": [
        (16, 62, 35, 273),                # left brick wall
        (603, 62, 622, 273),              # right brick wall
        (35, 69, 603, 82),                # ceiling (incl. the hatches)
        (35, 251, 189, 273),              # left floor
        [(188, 251), (237, 212), (400, 212), (449, 251), (449, 273), (188, 273)],   # raised middle with stairs
        (449, 251, 603, 273),             # right floor
    ],
    "ledges": [
        (35, 150, 185), (487, 602, 185),   # side galleries
        (143, 193, 145), (442, 492, 145),  # shelves at the ladder tops
        (274, 364, 168), (274, 364, 190),  # middle racks
        (72, 114, 207), (72, 114, 229),    # left rack
        (521, 563, 207), (521, 563, 229),  # right rack
    ],
    "ladders": [(131, 141, 131, 185), (494, 504, 131, 185)],
    "hanging": [("crate_hanging", 318, 82, 40), ("lamp", 200, 82, 30), ("lamp", 440, 82, 30), ("lamp", 90, 82, 26)],
    "belts": [(42, 138, 251, 0.7), (500, 596, 251, -0.7)],
    "spawns": [(80, 251), (560, 251), (318, 212), (100, 185), (540, 185), (318, 168),
               (168, 145), (467, 145), (93, 207), (542, 207)],
    "weapons": [(318, 212), (60, 251), (580, 251), (168, 145), (467, 145), (318, 190)],
    "props": [("crate", 250, 204), ("crate", 388, 204), ("explosive_barrel", 150, 243.5),
              ("explosive_barrel", 490, 243.5), ("crate", 93, 199), ("crate", 300, 160),
              ("barrel", 60, 243.5), ("computer_desktop", 330, 206), ("table", 560, 246),
              ("crate", 542, 199)],
}

# Sewers: offices left and right, catwalks and ladders over an acid pool (burns, ladders lead out).
LEVELS["sewers"] = {
    "title": "Sewers",
    "art": "sewers.png",             # original bitmap #2221 (616x390)
    "bounds": (8, 12, 592, 330),
    "deathY": 360,
    "acid": (153, 513, 312),
    "pool_ladders": [(155, 164, 286), (502, 511, 300)],
    "hanging": [("crate_hanging", 295, 34, 44), ("lamp", 100, 58, 24), ("lamp", 500, 96, 24)],
    "solid": [
        # left side
        (8, 46, 34, 251),                 # brick wall
        (34, 46, 161, 58),                # office ceiling
        (161, 22, 173, 94),               # wall stub
        (34, 130, 173, 142),              # office floor
        [(34, 250), (48, 250), (94, 286), (94, 298), (34, 298)],        # landing + stairs down
        (94, 286, 153, 298),              # lower floor
        (142, 298, 153, 345),             # pool walls and bottom, under the surface
        (513, 300, 524, 345),
        (153, 326, 513, 345),
        [(78, 246), (100, 223), (104, 223), (82, 246)],      # steps up to the shelf
        # middle
        (161, 22, 429, 34),               # ceiling
        (417, 34, 429, 136),              # wall stub
        # right side
        (429, 84, 566, 96),               # office ceiling
        (417, 174, 566, 186),             # office floor
        (566, 84, 592, 312),              # brick wall
        (513, 300, 566, 312),             # floor in the bottom-right corner (reached by the exit door)
    ],
    "ledges": [
        (104, 169, 222),                  # left shelf
        (225, 288, 130),                  # upper catwalk
        (285, 350, 200),                  # lower catwalk
        (352, 424, 296),                  # red raft
        (153, 166, 286),                  # steps out of the pool
        (500, 513, 300),
    ],
    "ladders": [(291, 302, 112, 200), (355, 366, 184, 326), (155, 164, 286, 326), (502, 511, 300, 326)],
    "doors": [((42, 130), (40, 250)), ((558, 174), (556, 300))],
    "spawns": [(100, 130), (480, 174), (256, 130), (318, 200), (130, 222), (120, 286),
               (540, 300), (388, 296)],
    "weapons": [(60, 130), (530, 174), (256, 130), (318, 200), (388, 296), (120, 286)],
    "props": [("crate", 60, 122), ("explosive_barrel", 450, 166.5), ("crate", 530, 292),
              ("desk", 95, 123), ("computer_monitor", 92, 110), ("chair", 115, 121.5),
              ("filecab", 500, 166.5), ("table", 470, 169)],
    "glass": [(167, 94, 130), (423, 136, 174)],
}

# Rooftops: gravel roof with a power tower, three-storey building on the right, night sky.
LEVELS["rooftops"] = {
    "title": "Rooftops",
    "art": "rooftops.png",           # original bitmap #2219 (920x460, transparent sky)
    "sky": [{"image": "images/levels/sky_city.png", "x": 0, "y": 0, "parallax": 0.2}],
    "bounds": (20, 92, 866, 404),
    "deathY": 430,
    "hanging": [("crate_hanging", 640, 180, 34), ("lamp", 640, 294, 24), ("lamp", 790, 306, 24)],
    "solid": [
        (10, 60, 35, 420),                # left edge of the roof
        (35, 378, 476, 420),              # gravel
        (119, 294, 130, 378),             # lamp post
        (476, 366, 552, 420),             # concrete blocks
        # building
        (552, 168, 709, 180),             # roof
        (552, 180, 564, 221),             # roof edge wall
        (709, 168, 721, 221),             # stairwell wall
        (758, 168, 847, 180),             # roof (right of the stairwell)
        (796, 146, 845, 168),             # air conditioners
        (552, 258, 847, 270),             # upper floor
        (552, 270, 564, 331),             # wall
        (564, 282, 717, 294),             # red room ceiling
        (705, 294, 718, 336),             # wall stub
        (718, 294, 847, 306),             # ceiling (right)
        [(552, 366), (709, 366), (730, 384), (730, 420), (552, 420)],   # red room floor + steps
        (730, 384, 847, 420),             # lower floor
        (847, 60, 876, 420),              # right brick wall
    ],
    "ledges": [
        (229, 258, 271), (285, 315, 271), (209, 259, 315),  # tower arms
        (722, 740, 191),                  # shelf below the stairwell
        (727, 769, 214), (727, 769, 236), # scaffold rack
    ],
    "ladders": [(266, 278, 241, 378)],
    "doors": [((839, 258), (839, 384))],
    "spawns": [(80, 378), (200, 378), (380, 378), (240, 271), (300, 271), (500, 366),
               (620, 366), (800, 384), (620, 258), (780, 258), (650, 168), (820, 146)],
    "weapons": [(150, 378), (330, 378), (234, 315), (640, 366), (780, 384), (680, 258), (800, 168)],
    "props": [("crate", 420, 370), ("explosive_barrel", 590, 358.5), ("crate", 790, 376),
              ("crate", 600, 250), ("desk", 660, 251), ("computer_monitor", 655, 238),
              ("chair", 682, 249.5), ("filecab", 760, 250.5), ("barrel", 70, 370.5),
              ("explosive_barrel", 400, 370.5)],
    "glass": [(558, 221, 258), (558, 331, 366)],
}

# Container room: small arena on top of green shipping containers.
LEVELS["containers"] = {
    "title": "Containers",
    "art": "containers.png",         # original bitmap #2451 (347x222)
    "bounds": (0, 0, 347, 222),
    "solid": [
        (0, 0, 21, 222),                  # left brick wall
        (327, 0, 347, 222),               # right brick wall
        (21, 4, 327, 16),                 # ceiling
        (21, 148, 207, 222),              # low containers
        (207, 111, 327, 222),             # high containers
    ],
    "spawns": [(50, 148), (130, 148), (190, 148), (240, 111), (310, 111)],
    "weapons": [(90, 148), (160, 148), (270, 111)],
    "props": [("crate", 120, 140), ("explosive_barrel", 300, 103.5), ("crate", 230, 103),
              ("crate", 230, 87), ("barrel", 60, 140.5), ("explosive_barrel", 175, 140.5)],
}


if __name__ == "__main__":
    for name, level in LEVELS.items():
        build(name, level)
