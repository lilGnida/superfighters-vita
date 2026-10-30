#!/usr/bin/env python3
"""Adds the invisible level-collision objects to assets/objects.json (idempotent).

wall  - solid static block (concrete material)
ledge - one-way platform (jump through from below, drop through with double-down)
climb - ladder area
Size comes from the map object's "size" / "verts"; they are drawn by the level art layer.
"""
import json, os, sys
from PIL import Image

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")
img = os.path.join(root, "images", "collider.png")
if not os.path.exists(img):
    Image.new("RGBA", (1, 1), (0, 0, 0, 0)).save(img)

path = os.path.join(root, "objects.json")
data = json.load(open(path))
defs = {
    "wall": {"image": {"image": "images/collider.png"}, "material": "concrete",
             "matter": {"isStatic": True}, "cls": "BaseObject", "group": "static"},
    "ledge": {"image": {"image": "images/collider.png"}, "cls": "Platform",
              "matter": {"isStatic": True}, "group": "platform"},
    "climb": {"image": {"image": "images/collider.png"}, "cls": "Ladder",
              "matter": {"isStatic": True}, "group": "none"},
    "glass": {"image": {"image": "images/glass.png"}, "health": 1, "cls": "BaseObject",
              "matter": {"isStatic": True}, "group": "static",
              "sounds": {"killed": [{"sound": "sounds/bust_glass00.ogg", "volume": 1, "loop": False},
                                    {"sound": "sounds/bust_glass01.ogg", "volume": 1, "loop": False}]},
              "onkill": {"fn": True, "name": "onkill"}},
    "glass_shard": {"image": {"image": "images/glass_shard.png"}, "health": 1, "cls": "BaseObject",
                    "lifeTime": 120, "fade": True, "group": "debris"},
    "exit_door": {"image": {"image": "images/collider.png"}, "cls": "Door",
                  "matter": {"isStatic": True, "isSensor": True}, "group": "none"},
}
for k, v in defs.items():
    data["objects"][k] = v
    if k not in data["order"]:
        data["order"].append(k)
json.dump(data, open(path, "w"), indent=1)
print("ok", ", ".join(defs))
