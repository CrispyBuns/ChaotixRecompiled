#!/usr/bin/env python3
"""Whole-bank sprite sheets with the real ROM.

Exports the sheets of the characters' banks straight from the ROM (no frame
played), then loads them, unchanged, as a pack: no cell was edited, so
nothing is replaced. With the cells' hashes taken out of the descriptions,
every frame of every character is replaced by its own cell instead. Either
way, in a level with two characters and in the lobby every picture must be
exactly the original one, and the recompiled machine must stay bit-identical
to the interpreter with the pack on both. Widescreen on and off.

Then Knuckles' body sheet (bank 02) is recoloured: its 113 cells load, a
level with Knuckles changes, still in lockstep, and a level without him does
not.

usage: test_sprite_sheets.py HEADLESS ROM
"""
import glob
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

FRAMES = 1500
SCENES = {
    "knuckles_espio": ["--stage", "300:0:1:0:2:7:0", "--fuzz", "5:700:1500"],
    "mighty_espio": ["--stage", "300:0:1:0:0:7:0", "--fuzz", "5:700:1500"],
    "lobby": ["--stage", "300:7:0:0:2:7:0", "--fuzz", "2:700:1500"],
}


def run(*args, frames=FRAMES):
    r = subprocess.run([HEADLESS, "--rom", ROM, "--frames", str(frames), "--hash-every", "1", *args],
                       capture_output=True, text=True, timeout=1800)
    out = r.stdout + r.stderr
    return out, dict(re.findall(r"^hash (\d+) ([0-9a-f]+)$", out, re.M))


def png_chunks(data):
    pos = 8
    while pos < len(data):
        n, = struct.unpack(">I", data[pos:pos + 4])
        yield data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        pos += 12 + n


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def recolour(src, dst, index):
    """Rewrites an 8-bit indexed sheet with every graphic pixel (1-0x7F) set to index; the grid stays."""
    with open(src, "rb") as f:
        data = f.read()
    chunks = list(png_chunks(data))
    width, height = struct.unpack(">II", dict(chunks)[b"IHDR"][:8])
    raw = bytearray(zlib.decompress(b"".join(body for kind, body in chunks if kind == b"IDAT")))
    stride = width + 1
    changed = 0
    for y in range(height):
        assert raw[y * stride] == 0, "the sheets are written unfiltered"
        for x in range(width):
            i = y * stride + 1 + x
            if 0 < raw[i] < 0x80:
                raw[i] = index
                changed += 1
    out = data[:8]
    for kind, body in chunks:
        if kind == b"IDAT":
            continue
        if kind == b"IEND":
            out += chunk(b"IDAT", zlib.compress(bytes(raw)))
        out += chunk(kind, body)
    with open(dst, "wb") as f:
        f.write(out)
    return changed


class SpriteSheets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.mkdtemp(prefix="chaotix_sheets_")
        cls.sheets = os.path.join(cls.dir, "sheets")
        out, _ = run("--export-sheets", cls.sheets, frames=0)
        cls.export_log = out
        cls.plain = {}

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def plain_hashes(self, scene, wide):
        key = (scene, wide)
        if key not in self.plain:
            out, hashes = run(*SCENES[scene], "--wide", str(wide))
            self.assertEqual(len(hashes), FRAMES, out[-2000:])
            self.plain[key] = hashes
        return self.plain[key]

    def test_a_export(self):
        # Every character's two banks that have graphics (bank 1 and 9 have
        # none), with their descriptions.
        self.assertIn("sheets: 14 written", self.export_log, self.export_log[-2000:])
        names = sorted(os.path.basename(p) for p in glob.glob(os.path.join(self.sheets, "32x_*.png")))
        self.assertEqual(names, ["32x_%02x.png" % b for b in range(16) if b not in (1, 9)])
        for n in names:
            self.assertTrue(os.path.exists(os.path.join(self.sheets, n[:-4] + ".txt")), n)
        self.assertIn("32x_02.png: 113 frames, 113 graphics", self.export_log)

    def unhashed(self):
        """The sheets with descriptions that carry no hashes: every cell loads."""
        d = os.path.join(self.dir, "unhashed")
        if not os.path.isdir(d):
            os.makedirs(d)
            for f in glob.glob(os.path.join(self.sheets, "32x_*")):
                if f.endswith(".png"):
                    shutil.copy(f, d)
                    continue
                with open(f, encoding="utf-8") as src:
                    lines = [line.split() for line in src if not line.startswith("#")]
                with open(os.path.join(d, os.path.basename(f)), "w", encoding="utf-8") as dst:
                    for w in lines:
                        if w and w[0] == "frame" and len(w) == 11:
                            w = w[:10]
                        dst.write(" ".join(w) + "\n")
        return d

    def check_unchanged(self, scene, wide, pack, loaded):
        plain = self.plain_hashes(scene, wide)
        out, packed = run(*SCENES[scene], "--wide", str(wide), "--tiles", pack, "--lockstep")
        self.assertIn("sprites: %d replacement(s) loaded" % loaded, out, out[-2000:])
        self.assertIn("lockstep: %d frames bit-identical" % FRAMES, out, out[-2000:])
        if loaded:
            composed = int(re.search(r"sprites: (\d+) pictures composed", out).group(1))
            self.assertGreater(composed, 100, out[-2000:])
        differ = [f for f in sorted(plain, key=int) if packed.get(f) != plain[f]]
        self.assertEqual(differ[:5], [], "%s, wide %d: %d of %d pictures differ" % (scene, wide, len(differ), len(plain)))

    def test_b_level_unchanged(self):
        # As exported: no cell edited, so nothing is replaced at all.
        for wide in (53, 0):
            self.check_unchanged("knuckles_espio", wide, self.sheets, 0)
        # Without the hashes every cell is a replacement, and each must
        # draw exactly its original.
        self.check_unchanged("knuckles_espio", 53, self.unhashed(), 969)

    def test_c_lobby_unchanged(self):
        self.check_unchanged("lobby", 53, self.sheets, 0)
        self.check_unchanged("lobby", 53, self.unhashed(), 969)

    def test_d_a_recoloured_sheet_changes_that_character_only(self):
        mod = os.path.join(self.dir, "knuckles_recoloured")
        os.makedirs(mod, exist_ok=True)
        for f in glob.glob(os.path.join(self.sheets, "32x_*")):
            shutil.copy(f, mod)
        self.assertGreater(recolour(os.path.join(self.sheets, "32x_02.png"), os.path.join(mod, "32x_02.png"), 7), 10000)
        plain = self.plain_hashes("knuckles_espio", 53)
        out, changed = run(*SCENES["knuckles_espio"], "--wide", "53", "--tiles", mod, "--lockstep")
        self.assertIn("sprites: 113 replacement(s) loaded", out, out[-2000:])  # bank 02's cells only
        self.assertIn("lockstep: %d frames bit-identical" % FRAMES, out, out[-2000:])
        differ = sum(1 for f in plain if changed.get(f) != plain[f])
        self.assertGreater(differ, len(plain) // 2, "Knuckles did not change")
        plain = self.plain_hashes("mighty_espio", 53)
        out, other = run(*SCENES["mighty_espio"], "--wide", "53", "--tiles", mod)
        differ = [f for f in sorted(plain, key=int) if other.get(f) != plain[f]]
        self.assertEqual(differ[:5], [], "a level without Knuckles changed")


if __name__ == "__main__":
    unittest.main()
