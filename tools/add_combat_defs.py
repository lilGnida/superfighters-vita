#!/usr/bin/env python3
"""Adds the corpse object and per-gun recoil / knockback to assets/objects.json (idempotent).

recoil    - push on the shooter per shot (px/frame)
knockback - push on whoever a single bullet hits (px/frame); 2+ knocks a fighter off their feet
"""
import json, os

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "objects.json")
data = json.load(open(path))

data["objects"]["corpse"] = {"image": {"image": "images/player.png"}, "frameCount": {"x": 10, "y": 9},
                             "cls": "Corpse", "group": "debris"}
if "corpse" not in data["order"]:
    data["order"].append("corpse")

GUNS = {  # recoil, knockback
    "pistol": (0, 0.9), "uzi": (0, 0.7), "magnum": (0.5, 2.6), "rifle": (0.1, 1.0),
    "shotgun": (0.6, 1.0), "sniper": (0.8, 4.5), "flamethrower": (0, 0), "bazooka": (1.0, 0),
}
for key, (recoil, knockback) in GUNS.items():
    data["objects"][key]["recoil"] = recoil
    data["objects"][key]["knockback"] = knockback

json.dump(data, open(path, "w"), indent=1)
