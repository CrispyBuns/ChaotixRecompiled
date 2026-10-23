#!/usr/bin/env python3
"""32X sprite replacement with the real ROM (issue #35).

Dumps the graphics the SH-2 draws in a level, the title, the lobby, the bonus
stage and the special stage, then loads each dump, unchanged, as a pack: every
graphic is replaced by itself, so every picture must be exactly the original
one (compared frame by frame), and the recompiled machine must stay
bit-identical to the interpreter with the pack on both. Anything wrong in
naming, loading, the replacement graphics, the write tracking or the
composition over polygons, fills and the widescreen margins shows up as a
difference. Widescreen on and off.

A pack with every sprite pixel changed must then change the 32X layer and
leave the Mega Drive layer, and what polygons and fills drew, as they were.

usage: test_sprite_pack.py HEADLESS ROM
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

# name: (arguments, frames)
SCENES = {
    "level": (["--stage", "300:0:1:0:2:7:0", "--fuzz", "5:700:1500"], 1500),
    "title": ([], 1900),
    "lobby": (["--stage", "300:7:0:0:2:7:0", "--fuzz", "2:700:1500"], 1500),
    "bonus": (["--stage", "300:9:0:0:2:7:0", "--fuzz", "3:700:1500"], 1500),
    "special": (["--stage", "300:10:0:0:2:7:0", "--fuzz", "5:700:1500"], 1500),
}


def run(scene, *args):
    extra, frames = SCENES[scene]
    r = subprocess.run([HEADLESS, "--rom", ROM, "--frames", str(frames), "--hash-every", "1", *extra, *args],
                       capture_output=True, text=True, timeout=1200)
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
    """Rewrites an 8-bit indexed PNG (as dumped) with every non-zero pixel set to index."""
    with open(src, "rb") as f:
        data = f.read()
    chunks = list(png_chunks(data))
    width, height = struct.unpack(">II", dict(chunks)[b"IHDR"][:8])
    raw = bytearray(zlib.decompress(b"".join(body for kind, body in chunks if kind == b"IDAT")))
    stride = width + 1
    for y in range(height):
        assert raw[y * stride] == 0, "the dumps are written unfiltered"
        for x in range(width):
            i = y * stride + 1 + x
            if raw[i]:
                raw[i] = index
    out = data[:8]
    for kind, body in chunks:
        if kind == b"IDAT":
            continue
        if kind == b"IEND":
            out += chunk(b"IDAT", zlib.compress(bytes(raw)))
        out += chunk(kind, body)
    with open(dst, "wb") as f:
        f.write(out)


class SpritePack(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.mkdtemp(prefix="chaotix_sprites_")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def dump(self, scene, wide):
        d = os.path.join(self.dir, "%s_%d" % (scene, wide))
        if not os.path.isdir(d):
            os.makedirs(d)
            out, hashes = run(scene, "--wide", str(wide), "--dump-graphics", d)
            self.assertGreater(len(hashes), 1000, out[-2000:])
            with open(os.path.join(d, "hashes.txt"), "w", encoding="utf-8") as f:
                f.write("\n".join("%s %s" % kv for kv in sorted(hashes.items())))
        with open(os.path.join(d, "hashes.txt"), encoding="utf-8") as f:
            hashes = dict(line.split() for line in f)
        return d, hashes

    def check_unchanged(self, scene, wide):
        d, plain = self.dump(scene, wide)
        sprites = glob.glob(os.path.join(d, "32x_*.png"))
        self.assertGreater(len(sprites), 10, scene)
        out, packed = run(scene, "--wide", str(wide), "--tiles", d, "--lockstep")
        self.assertIn("sprites: %d replacement(s) loaded" % len(sprites), out, out[-2000:])
        self.assertIn("bit-identical", out, out[-2000:])
        composed = int(re.search(r"sprites: (\d+) pictures composed", out).group(1))
        self.assertGreater(composed, 100, out[-2000:])
        differ = [f for f in sorted(plain, key=int) if packed.get(f) != plain[f]]
        self.assertEqual(differ[:5], [], "%s, wide %d: %d of %d pictures differ" % (scene, wide, len(differ), len(plain)))

    def test_level(self):
        for wide in (53, 0):
            self.check_unchanged("level", wide)

    def test_title(self):
        for wide in (53, 0):
            self.check_unchanged("title", wide)

    def test_lobby(self):
        for wide in (53, 0):
            self.check_unchanged("lobby", wide)

    def test_bonus_stage(self):
        for wide in (53, 0):
            self.check_unchanged("bonus", wide)

    def test_special_stage(self):
        for wide in (53, 0):
            self.check_unchanged("special", wide)

    def test_z_changed_sprites_leave_the_rest(self):
        # The bonus stage draws polygons and sprites in one layer; the level
        # draws sprites into the widescreen margins.
        for scene, wide in (("bonus", 0), ("level", 53)):
            d, plain = self.dump(scene, wide)
            mod = os.path.join(self.dir, "%s_%d_changed" % (scene, wide))
            os.makedirs(mod, exist_ok=True)
            for f in glob.glob(os.path.join(d, "32x_*.png")):
                recolour(f, os.path.join(mod, os.path.basename(f)), 7)
            out, changed = run(scene, "--wide", str(wide), "--tiles", mod, "--lockstep", "--sprites-check")
            self.assertIn("bit-identical", out, out[-2000:])
            self.assertIn("sprites check: 0 bytes of polygons and fills shown changed", out, out[-2000:])
            differ = sum(1 for f in plain if changed.get(f) != plain[f])
            self.assertGreater(differ, len(plain) // 2, "%s: the sprites did not change" % scene)
            _, md_plain = run(scene, "--wide", str(wide), "--layer", "md")
            _, md_changed = run(scene, "--wide", str(wide), "--tiles", mod, "--layer", "md")
            self.assertEqual(md_plain, md_changed, "%s: the Mega Drive layer changed" % scene)


if __name__ == "__main__":
    unittest.main()
