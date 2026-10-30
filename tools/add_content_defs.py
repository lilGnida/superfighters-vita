#!/usr/bin/env python3
"""Adds the new weapons and interactive props to assets/objects.json (idempotent)."""
import copy, json, os

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "objects.json")
data = json.load(open(path))
o = data["objects"]
uzi, bazooka, pistol = o["uzi"], o["bazooka"], o["pistol"]
SHARD = {"killed": copy.deepcopy(o["glass"]["sounds"]["killed"])}


def gun(base, image, **kw):
    d = copy.deepcopy(base)
    d["image"] = {"image": image}
    d.update(kw)
    return d


def melee(base, image, **kw):
    d = copy.deepcopy(o[base])
    d["image"] = {"image": image}
    d.update(kw)
    return d


def thrown(image, projectile, **kw):
    d = copy.deepcopy(o["grenades"])
    d["image"] = {"image": image}
    d["projectile"] = {"ref": projectile}
    d.pop("detonationTime", None)
    d.update(kw)
    return d


new = {
    "smg": gun(uzi, "images/weapon/smg.png", ammo=30, burst=3, damage=5, speed=15, timing=12, spread=9, recoil=0, knockback=0.6),
    "lobbed_grenade": dict(copy.deepcopy(o["grenade"]), lifeTime=75),
    "grenade_launcher": gun(bazooka, "images/weapon/grenade_launcher.png", ammo=4, speed=7, timing=36, spread=2,
                            projectile={"ref": "lobbed_grenade"}, recoil=0.5, knockback=0),
    "taser_bolt": {"image": {"image": "images/taser_bolt.png"}, "cls": "Projectile", "group": "projectile", "stun": 70},
    "taser": gun(pistol, "images/weapon/taser.png", ammo=5, damage=4, speed=12, timing=40, spread=1,
                 projectile={"ref": "taser_bolt"}, recoil=0, knockback=0),
    "bat": melee("sword", "images/weapon/bat.png", damage={"hp": 12}, knockback=2.6),
    "knife": melee("machete", "images/weapon/knife.png", damage={"hp": 9, "sound": "cut"}, thrownDamage=32),
    "plank": melee("machete", "images/weapon/plank.png", damage={"hp": 9}, durability=5),
    "pipe_club": melee("machete", "images/weapon/pipe_club.png", damage={"hp": 12}, durability=7, knockback=1.5),
    "mine": {"image": {"image": "images/mine.png"}, "cls": "Mine", "group": "item", "health": 1,
             "damageModifier": {"melee": 0, "collision": 0}, "onkill": {"fn": True, "name": "onkill"}},
    "mines": thrown("images/weapon/mines.png", "mine", ammo=2),
    "flashbang": dict(copy.deepcopy(o["grenade"]), image={"image": "images/flashbang.png"}),
    "flashbangs": thrown("images/weapon/flashbangs.png", "flashbang", ammo=2, detonationTime=70),
    "shuriken": {"image": {"image": "images/shuriken.png"}, "cls": "Projectile", "group": "projectile", "plain": True},
    "shurikens": thrown("images/weapon/shurikens.png", "shuriken", ammo=5, damage=15, speed=9),
    "anchor": {"image": {"image": "images/collider.png"}, "cls": "Marker", "matter": {"isStatic": True}, "group": "none"},
    "lamp": {"image": {"image": "images/lamp.png"}, "cls": "Hanging", "group": "dynamic_active", "health": 1,
             "material": "metal", "sounds": SHARD, "onkill": {"fn": True, "name": "onkill"}},
    "range_target": dict(copy.deepcopy(o["target"]), group="dynamic_active"),
    "conveyor": {"image": {"image": "images/conveyor.png"}, "cls": "Conveyor", "matter": {"isStatic": True}, "group": "none"},
}
for key, d in new.items():
    o[key] = d
    if key not in data["order"]:
        data["order"].append(key)

o["crate_hanging"]["cls"] = "Hanging"
o["crate_hanging"]["health"] = 60
o["glass_shard"]["lifeTime"] = 600
json.dump(data, open(path, "w"), indent=1)
