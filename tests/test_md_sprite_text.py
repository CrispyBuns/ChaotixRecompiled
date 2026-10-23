#!/usr/bin/env python3
"""Mega Drive text replaced by sprite (mdsprite_<name>.txt + .png), with the
real ROM.

The dump lists the sprites on the DATA LOAD screen in groups; the banner's
letters (DATA LOAD: four 4x3 sprites side by side) become a pattern with a
synthetic solid image. With it, the DATA LOAD picture changes inside the
banner's letters' box and nowhere else, the title before it not at all, and
the recompiled machine stays bit-identical to the interpreter. Without
patterns (and with a pattern that matches nothing) the pictures are
byte-identical to a run without the replacer. A group beats a pattern of one
of its sprites.

No ROM-derived pixels are written by the test: the pattern holds hashes
taken from the dump, the images are solid colours.

usage: test_md_sprite_text.py HEADLESS ROM
"""
import glob
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

HEADLESS, ROM = sys.argv[1:3]
del sys.argv[1:3]

PRESS = ["--press", "1800:start:6", "--press", "1950:start:6"]
FRAMES = "2110"
TITLE, MENU = 1700, 2100
SHOTS = "%d,%d" % (TITLE, MENU)


def read_png(path):
    """The headless writer's PNGs: 8-bit RGB(A), any filter."""
    with open(path, "rb") as f:
        data = f.read()
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype in (2, 6), (depth, ctype)
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)])
        prev = line
    return rows


def write_indexed_png(path, width, height, pixels, palette):
    """An 8-bit palette-indexed PNG; `pixels` is a list of rows of indices."""
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(row) for row in pixels)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)))
        f.write(chunk(b"PLTE", b"".join(bytes(c) for c in palette)))
        f.write(chunk(b"IDAT", zlib.compress(raw)))
        f.write(chunk(b"IEND", b""))


def solid_png(path, width, height, index):
    write_indexed_png(path, width, height, [[index] * width] * height, [(0, 0, 0)] + [(255, 255, 255)] * 15)


def write_rgb_png(path, width, height, colour):
    """A full-colour PNG, which patterns refuse."""
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(colour) * width for _ in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw)))
        f.write(chunk(b"IEND", b""))


def run(*args):
    r = subprocess.run([HEADLESS, "--rom", ROM, "--frames", FRAMES, *PRESS, *args],
                       capture_output=True, text=True, timeout=900)
    return r.stdout + r.stderr


def groups_in(dump_dir):
    """Every group listed in the dump: (x, y, w, h, [sprite lines])."""
    groups = []
    for path in sorted(glob.glob(os.path.join(dump_dir, "Text", "sprites_*.txt"))):
        with open(path, encoding="utf-8") as f:
            text = f.read()
        for m in re.finditer(r"# group \d+: screen x (-?\d+) y (-?\d+), .*?, (\d+)x(\d+) pixels.*\n"
                             r"(?:#.*\n)*((?:sprite .*\n)+)", text):
            lines = m.group(5).strip().split("\n")
            groups.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), lines))
    return groups


class MdSpriteText(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.mkdtemp(prefix="chaotix_mdsprite_")
        dump = os.path.join(cls.dir, "dump")
        run("--dump-graphics", dump)
        cls.dump = dump
        cls.groups = groups_in(dump)
        cls.plain = os.path.join(cls.dir, "plain")
        os.makedirs(cls.plain)
        run("--shot", SHOTS, "--out", cls.plain)

    def banner(self):
        """DATA LOAD's letters: four 4x3 sprites in a row, at the top."""
        found = [g for g in self.groups if len(g[4]) == 4 and all(" 4x3 " in s for s in g[4]) and g[1] < 40]
        self.assertTrue(found, "no DATA LOAD group in the dump: %r" % [g[:4] for g in self.groups])
        return found[0]

    def mods(self, files):
        d = tempfile.mkdtemp(dir=self.dir)
        for name, content in files.items():
            if isinstance(content, str):
                with open(os.path.join(d, name), "w", encoding="utf-8") as f:
                    f.write(content)
            else:
                content(os.path.join(d, name))
        return d

    def shots(self, mods, *extra):
        out_dir = tempfile.mkdtemp(dir=self.dir)
        out = run("--tiles", mods, "--shot", SHOTS, "--out", out_dir, *extra)
        return out, out_dir

    def frame(self, d, n):
        return os.path.join(d, "frame_%05d.png" % n)

    def same_files(self, a, b):
        for n in (TITLE, MENU):
            with open(self.frame(a, n), "rb") as x, open(self.frame(b, n), "rb") as y:
                self.assertEqual(x.read(), y.read(), "frame %d" % n)

    def changed_box(self, out_dir):
        a, b = read_png(self.frame(self.plain, MENU)), read_png(self.frame(out_dir, MENU))
        xs, ys = [], []
        for y, (ra, rb) in enumerate(zip(a, b)):
            for x, (pa, pb) in enumerate(zip(ra, rb)):
                if pa != pb:
                    xs.append(x)
                    ys.append(y)
        return (min(xs), min(ys), max(xs), max(ys)) if xs else None

    def test_the_dump_lists_the_sprites(self):
        x, y, w, h, lines = self.banner()
        self.assertEqual((w, h), (128, 24))
        self.assertTrue(lines[0].startswith("sprite 0 0 4x3 "), lines[0])
        for line in lines:
            words = line.split()
            self.assertEqual(len(words), 4 + 12, line)
            self.assertTrue(all(re.fullmatch(r"[0-9a-f]{16}", t) for t in words[4:]), line)
        self.assertTrue(glob.glob(os.path.join(self.dump, "Text", "sprites_*.png")))

    def test_no_patterns_change_nothing(self):
        out, d = self.shots(self.mods({}))
        self.assertIn("text: 0 pattern(s) loaded", out, out[-2000:])
        self.same_files(self.plain, d)
        nowhere = self.mods({"mdsprite_nowhere.txt": "sprite 0 0 1x1 0123456789abcdef\n",
                             "mdsprite_nowhere.png": lambda p: solid_png(p, 8, 8, 1)})
        out, d = self.shots(nowhere)
        self.assertIn("text: 1 pattern(s) loaded", out, out[-2000:])
        self.same_files(self.plain, d)

    def test_the_banner_is_replaced_only_there(self):
        x, y, w, h, lines = self.banner()
        mods = self.mods({"mdsprite_dataload.txt": "# DATA LOAD\n" + "\n".join(lines) + "\n",
                          "mdsprite_dataload.png": lambda p: solid_png(p, w, h, 1)})
        out, d = self.shots(mods, "--lockstep")
        self.assertIn("text: 1 pattern(s) loaded", out, out[-2000:])
        self.assertIn("bit-identical", out, out[-2000:])
        with open(self.frame(self.plain, TITLE), "rb") as a, open(self.frame(d, TITLE), "rb") as b:
            self.assertEqual(a.read(), b.read(), "the title before DATA LOAD")
        box = self.changed_box(d)
        self.assertIsNotNone(box, "the banner did not change")
        self.assertGreaterEqual(box[0], x)
        self.assertGreaterEqual(box[1], y)
        self.assertLess(box[2], x + w)
        self.assertLess(box[3], y + h)
        # Solid: the box is one colour, except where a sprite in front of
        # the banner's letters or the 32X layer covers it.
        rows = read_png(self.frame(d, MENU))
        colours = {}
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                colours[rows[yy][xx]] = colours.get(rows[yy][xx], 0) + 1
        self.assertGreater(max(colours.values()), w * h * 9 // 10, colours)

        # A group beats one of its sprites alone, loaded first.
        mods2 = self.mods({"mdsprite_dataload.txt": "\n".join(lines) + "\n",
                           "mdsprite_dataload.png": lambda p: solid_png(p, w, h, 1),
                           "mdsprite_a_d.txt": lines[0] + "\n",
                           "mdsprite_a_d.png": lambda p: solid_png(p, 32, 24, 2)})
        out, d2 = self.shots(mods2)
        self.assertIn("text: 2 pattern(s) loaded", out, out[-2000:])
        self.same_files(d, d2)

        # A narrower image, centred: only its columns change.
        mods3 = self.mods({"mdsprite_dataload.txt": "align center\n" + "\n".join(lines) + "\n",
                           "mdsprite_dataload.png": lambda p: solid_png(p, 64, h, 1)})
        out, d3 = self.shots(mods3)
        box = self.changed_box(d3)
        self.assertIsNotNone(box)
        # The letters outside the image are not drawn either: what is behind
        # them shows, so the change can reach the whole box, but the image's
        # colour is only in its columns.
        rows = read_png(self.frame(d3, MENU))
        ink = rows[y + h // 2][x + w // 2]
        self.assertEqual(rows[y + h // 2][x + 32], ink)
        self.assertEqual(rows[y + h // 2][x + 95], ink)
        self.assertNotEqual(rows[y + h // 2][x + 31], ink)
        self.assertNotEqual(rows[y + h // 2][x + 96], ink)

    def test_malformed_patterns_are_refused(self):
        x, y, w, h, lines = self.banner()
        words = lines[0].split()
        mods = self.mods({"mdsprite_short.txt": " ".join(words[:-1]) + "\n",
                          "mdsprite_short.png": lambda p: solid_png(p, 32, 24, 1),
                          "mdsprite_size.txt": "sprite 0 0 5x1 0123456789abcdef\n",
                          "mdsprite_size.png": lambda p: solid_png(p, 8, 8, 1),
                          "mdsprite_colour.txt": lines[0] + "\n",
                          "mdsprite_colour.png": lambda p: write_rgb_png(p, 8, 8, (200, 10, 10))})
        out, d = self.shots(mods)
        self.assertIn("text: 0 pattern(s) loaded", out, out[-2000:])
        self.assertIn("mdsprite_short.txt: line 1: a 4x3 sprite has 12 tiles", out)
        self.assertIn("mdsprite_size.txt: line 1: a sprite is", out)
        self.assertIn("mdsprite_colour.png: must be a palette-indexed PNG", out)
        self.same_files(self.plain, d)


if __name__ == "__main__":
    unittest.main()
