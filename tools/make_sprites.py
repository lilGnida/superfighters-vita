#!/usr/bin/env python3
"""Draws the sprites of the weapons and props that have no art in the original assets."""
import os
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "images")
K, G, D, L, W = (0, 0, 0, 255), (120, 120, 130, 255), (70, 70, 80, 255), (170, 170, 180, 255), (235, 235, 235, 255)
B, T, Y, R, C = (130, 85, 45, 255), (90, 55, 30, 255), (240, 210, 90, 255), (200, 50, 40, 255), (90, 200, 255, 255)
_ = None


def sprite(path, rows, palette):
    h, w = len(rows), len(rows[0])
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            c = palette.get(ch)
            if c:
                im.putpixel((x, y), c)
    out = os.path.join(ROOT, path)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    im.save(out)


P = {"k": K, "g": G, "d": D, "l": L, "w": W, "b": B, "t": T, "y": Y, "r": R, "c": C}

sprite("weapon/smg.png", ["..kkkkkkkk.", "kkgggggggkk", "kdddddkkkk.", "..kdkkd....", "..kk.kd....", ".....kk...."], P)
sprite("weapon/grenade_launcher.png", ["..kkkkkkkkkkkk..", ".kgggggggggggdkk", "kbbddddddddddkkk", "kbbkkdkkkkkkkk..", "kk..kdk.........", "....kk.........."], P)
sprite("weapon/taser.png", ["kkkkkkkk.", "kyyyyygkc", "kkkddkkk.", "...kdk...", "...kk...."], P)
sprite("weapon/bat.png", ["..........kkkkkk", ".kkkkkkkkkbbbbbk", "ktttbbbbbbbbbbbk", ".kkkkkkkkkkkkkk."], P)
sprite("weapon/knife.png", ["kkk..kkkkk.", "kttkkllllwk", "kkk..kkkk.."], P)
sprite("weapon/plank.png", ["kkkkkkkkkkkkkkk.", "kbbbtbbbbbtbbbbk", "kkkkkkkkkkkkkkk."], P)
sprite("weapon/pipe_club.png", ["kkkkkkkkkkkkkkk.", "kglllllllllllgkk", "kkkkkkkkkkkkkkk."], P)
sprite("weapon/mines.png", ["...kkkk...", ".kkrrrrkk.", "kdddddddk", "kkkkkkkkk"], P)
sprite("weapon/flashbangs.png", ["..kkk..kkk..", ".kwwwk.kwwwk", ".kwwwk.kwwwk", ".kkkkk.kkkkk"], P)
sprite("weapon/shurikens.png", ["...k....k..", "..kgk..kgk.", "kkgdgkkgdgk", "..kgk..kgk.", "...k....k.."], P)
sprite("mine.png", ["..kkkk..", ".krrrrk.", "kddddddk", "kkkkkkkk"], P)
sprite("flashbang.png", ["kkk", "kwk", "kwk", "kkk"], P)
sprite("shuriken.png", ["...k...", "..kgk..", ".kldlk.", "kgdkdgk", ".kldlk.", "..kgk..", "...k..."], P)
sprite("taser_bolt.png", ["cwwcwwc"], P)
sprite("lamp.png", ["..kkk..", "..kdk..", ".kdddk.", "kyyyyyk", "kwyyywk", ".kyyyk.", "..kkk.."], P)
sprite("rope.png", ["k"], P)

# light of a hanging lamp: a soft cone
glow = Image.new("RGBA", (64, 40), (0, 0, 0, 0))
for y in range(40):
    half = 3 + y * 0.7
    for x in range(64):
        d = abs(x - 31.5)
        if d < half:
            a = int(46 * (1 - y / 40) * (1 - d / half) ** 0.5)
            glow.putpixel((x, y), (255, 240, 170, a))
glow.save(os.path.join(ROOT, "lamp_glow.png"))

# conveyor belt: two periods of 8 px, the game scrolls the source window
belt = Image.new("RGBA", (16, 3), (45, 45, 52, 255))
for x in range(16):
    belt.putpixel((x, 0), (150, 150, 150, 255) if x % 8 < 4 else (60, 60, 68, 255))
    belt.putpixel((x, 2), (20, 20, 24, 255))
belt.save(os.path.join(ROOT, "conveyor.png"))
