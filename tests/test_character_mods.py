#!/usr/bin/env python3
"""Character mods with the real ROM (Mods/Characters, docs/CHARACTER_MODS.md).

- An identity mod (every colour and every stat as the ROM has them) must give
  pictures byte-identical to no mod, keep lockstep, and keep the 68K native.
- A recoloured character must change only that character's pixels on the 32X
  layer, from its colours to the mod's, leave the Mega Drive layer as it was,
  and keep lockstep.
- A stats change (Knuckles' acceleration doubled) must change how fast he
  gets going, measured in his object's ground speed in work RAM, with
  lockstep holding and the 68K still native.

usage: test_character_mods.py HEADLESS ROM
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

HEADLESS, ROM = sys.argv[1:3]
del sys.argv[1:3]

KNUCKLES, ESPIO = 2, 7
NAMES = {0: "mighty", 2: "knuckles", 3: "charmy", 4: "vector", 5: "bomb", 6: "heavy", 7: "espio"}
# name: (offset, size), as in runtime/character_mods.cpp
STATS = {
    "abilities_a": (0x01, 1), "abilities_b": (0x02, 1), "speed_cap": (0x04, 2), "acceleration": (0x06, 2),
    "deceleration": (0x08, 2), "gravity": (0x0A, 4), "throw_speed": (0x10, 2), "tether_reach": (0x12, 2),
    "jump_release": (0x14, 2), "jump": (0x16, 2), "spin_dash_min": (0x18, 2), "spin_dash_max": (0x1A, 2),
    "spin_dash_step": (0x1C, 2), "wall_cling_time": (0x1E, 2), "wall_jump_speed": (0x20, 2),
    "glide_speed": (0x2A, 2), "glide_acceleration": (0x2C, 2), "glide_fall_speed": (0x2E, 2),
    "roll_friction": (0x36, 2), "air_dash_speed": (0x38, 2),
}
# Botanic Base 1 with Knuckles and Espio, in a level with random input.
LEVEL = ["--stage", "300:0:1:0:2:7:0"]
PLAYER = 0xAD48  # the player's object in that level (docs/CHARACTERS.md)


def word(rom, off):
    return struct.unpack(">H", rom[off:off + 2])[0]


def long(rom, off):
    return struct.unpack(">I", rom[off:off + 4])[0]


def colours(rom, cid):
    off = long(rom, 0x18E4 + 4 * cid) - 0x700000
    return [word(rom, off + 2 * i) & 0x7FFF for i in range(18)]


def cycle_sets(rom):
    return [[word(rom, 0xE7BA + 2 * (k * 5 + j)) & 0x7FFF for j in range(5)] for k in range(16)]


def record(rom, cid):
    return long(rom, 0xBE58 + 4 * cid) - 0x880000


def rgb(c):
    """A 32X colour as #RRGGBB, the way the compositor expands it."""
    def e(v):
        return (v << 3) | (v >> 2)
    return "#%02X%02X%02X" % (e(c & 31), e((c >> 5) & 31), e((c >> 10) & 31))


def displayed(c):
    """A 32X colour as the output image holds it (XRGB8888)."""
    def e(v):
        return (v << 3) | (v >> 2)
    return e(c & 31) << 16 | e((c >> 5) & 31) << 8 | e((c >> 10) & 31)


def identity(rom, cid):
    """Every colour and every stat as the ROM has them."""
    lines = ["[Palette]"] + ["%d = %s" % (i + 1, rgb(c)) for i, c in enumerate(colours(rom, cid))]
    if cid == ESPIO:
        lines += ["[Cycle]"] + ["%d = %s" % (k, " ".join(rgb(c) for c in s)) for k, s in enumerate(cycle_sets(rom))]
    lines.append("[Stats]")
    rec = record(rom, cid)
    for name, (off, size) in STATS.items():
        lines.append("%s = 0x%X" % (name, int.from_bytes(rom[rec + off:rec + off + size], "big")))
    return "\n".join(lines) + "\n"


def read_png(path):
    """The RGB pixels of a PNG as written by the headless (8-bit RGB or RGBA, any filter)."""
    with open(path, "rb") as f:
        data = f.read()
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, = struct.unpack(">I", data[pos:pos + 4])
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype in (2, 6), (depth, ctype)
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    out, prev = [], bytearray(stride)
    for y in range(h):
        ft = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1: line[i] = (line[i] + a) & 255
            elif ft == 2: line[i] = (line[i] + b) & 255
            elif ft == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        out.extend(line[x * bpp] << 16 | line[x * bpp + 1] << 8 | line[x * bpp + 2] for x in range(w))
    return w, h, out


def run(*args, timeout=1200):
    r = subprocess.run([HEADLESS, "--rom", ROM, *args], capture_output=True, text=True, timeout=timeout)
    return r.stdout + r.stderr


def hashes(out):
    return dict(re.findall(r"^hash (\d+) ([0-9a-f]+)$", out, re.M))


NATIVE_68K = r"68K native \d+ / interp \d+ \((9\d|100)\.\d% native\)"


class CharacterMods(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(ROM, "rb") as f:
            cls.rom = f.read()
        cls.dir = tempfile.mkdtemp(prefix="chaotix_characters_")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def mods(self, name, files):
        d = os.path.join(self.dir, name)
        os.makedirs(d, exist_ok=True)
        for cid, text in files.items():
            with open(os.path.join(d, NAMES[cid] + ".ini"), "w", encoding="utf-8") as f:
                f.write(text)
        return d

    def test_identity_changes_nothing(self):
        d = self.mods("identity", {cid: identity(self.rom, cid) for cid in NAMES})
        for wide in ([], ["--wide", "53"]):
            args = ["--frames", "1500", *LEVEL, "--fuzz", "11:700:1500", "--hash-every", "1", *wide]
            plain = hashes(run(*args))
            out = run(*args, "--characters", d, "--lockstep")
            self.assertIn("characters: 7 file(s), stats changed 0 byte(s), colours replaced", out, out[-3000:])
            self.assertIn("frames bit-identical", out, out[-3000:])
            self.assertRegex(out, NATIVE_68K)
            # Knuckles and Espio were both recognised and substituted.
            self.assertRegex(out, r"colour block\(s\) replaced at the end", out[-3000:])
            self.assertRegex(out, r", 2 colour block\(s\) replaced at the end")
            modded = hashes(out)
            self.assertEqual(len(plain), 1500)
            self.assertEqual(plain, modded)

    def shots(self, args, frames, outdir):
        os.makedirs(outdir, exist_ok=True)
        out = run(*args, "--out", outdir, "--shot", ",".join(str(f) for f in frames))
        return out, {f: read_png(os.path.join(outdir, "frame_%05d.png" % f)) for f in frames}

    def test_recolour_changes_only_that_character(self):
        knuckles = colours(self.rom, KNUCKLES)
        # Every colour turned around (red and blue swapped, green inverted).
        mod = [((c >> 10) & 31) | (31 - ((c >> 5) & 31)) << 5 | (c & 31) << 10 for c in knuckles]
        text = "[Palette]\n" + "".join("%d = 0x%04X\n" % (i + 1, c) for i, c in enumerate(mod))
        d = self.mods("recolour", {KNUCKLES: text})
        frames = [900, 1100, 1300, 1500]
        args = ["--frames", "1500", *LEVEL, "--fuzz", "12:700:1500"]
        before = {displayed(c) for c in knuckles}
        after = {displayed(c) for c in mod}
        mapping = {displayed(a): displayed(b) for a, b in zip(knuckles, mod)}
        for layer in ("32x", "md"):
            _, plain = self.shots([*args, "--layer", layer], frames, os.path.join(self.dir, "plain_" + layer))
            out, modded = self.shots([*args, "--layer", layer, "--characters", d], frames,
                                     os.path.join(self.dir, "mod_" + layer))
            changed = 0
            for f in frames:
                w, h, a = plain[f]
                _, _, b = modded[f]
                for x, y in zip(a, b):
                    if x == y:
                        continue
                    changed += 1
                    self.assertIn(x, before, "frame %d: a pixel not of Knuckles' colours changed" % f)
                    self.assertIn(y, after)
                    self.assertEqual(mapping[x], y)
            if layer == "md":
                self.assertEqual(changed, 0, "the Mega Drive layer changed")
            else:
                self.assertGreater(changed, 1000, "Knuckles was not recoloured")
        out = run(*args, "--characters", d, "--lockstep", "--wide", "53")
        self.assertIn("frames bit-identical", out, out[-3000:])

    def ground_speed(self, out):
        return {int(f): int.from_bytes(bytes.fromhex(v), "big", signed=True)
                for f, v in re.findall(r"^peek (\d+) %04X ([0-9A-F]+)$" % (PLAYER + 0x2C), out, re.M)}

    def test_doubled_acceleration_moves_faster(self):
        rec = record(self.rom, KNUCKLES)
        accel = word(self.rom, rec + 6)
        d = self.mods("stats", {KNUCKLES: "[Stats]\nacceleration = %d\n" % (2 * accel)})
        args = ["--frames", "900", *LEVEL, "--press", "830:right:70", "--peek-every", "1:%04X:2" % (PLAYER + 0x2C)]
        plain = self.ground_speed(run(*args))
        out = run(*args, "--characters", d, "--lockstep")
        self.assertIn("stats changed 1 byte(s)", out, out[-3000:])
        self.assertIn("frames bit-identical", out, out[-3000:])
        self.assertRegex(out, NATIVE_68K)
        modded = self.ground_speed(out)
        # Before the press nothing differs; a few frames into it the ground
        # speed has grown by twice the steps.
        self.assertEqual(plain[825], modded[825])
        p = plain[845] - plain[835]
        m = modded[845] - modded[835]
        self.assertGreater(p, 0)
        self.assertEqual(m, 2 * p, (p, m))


if __name__ == "__main__":
    unittest.main()
