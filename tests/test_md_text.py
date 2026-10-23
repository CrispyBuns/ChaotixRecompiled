#!/usr/bin/env python3
"""Mega Drive text replaced by tile sequence (mdtext_<name>.txt + .png),
with the real ROM.

The dump lists the runs of plane tiles on the title menu; the run for
OPTIONS (seven tiles, the first and fifth the same O) becomes a pattern with
a synthetic solid image. With it, the menu picture changes inside the
OPTIONS box and nowhere else, the title before the menu not at all, and the
recompiled machine stays bit-identical to the interpreter. Without patterns
(and with a pattern that matches nothing) the pictures are byte-identical to
a run without the replacer. A longer pattern beats a shorter one over the
same tiles, and a wider centred image reaches past the run.

No ROM-derived pixels are written by the test: the pattern holds hashes
taken from the dump, the images are solid colours.

usage: test_md_text.py HEADLESS ROM
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

PRESS = ["--press", "1750:start:5"]
FRAMES = "1860"
TITLE, MENU = 1700, 1850
SHOTS = "%d,%d" % (TITLE, MENU)


def run(*args):
    r = subprocess.run([HEADLESS, "--rom", ROM, "--frames", FRAMES, *PRESS, *args],
                       capture_output=True, text=True, timeout=900)
    return r.stdout + r.stderr


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


def solid(path, width, index, clear_left=0):
    rows = [[0] * clear_left + [index] * (width - clear_left) for _ in range(8)]
    write_indexed_png(path, width, 8, rows, [(0, 0, 0)] + [(255, 255, 255)] * 15)


def runs_in(dump_dir):
    """Every run listed in the dump: (plane, x, y, [tokens])."""
    runs = []
    for path in sorted(glob.glob(os.path.join(dump_dir, "Text", "text_*.txt"))):
        with open(path, encoding="utf-8") as f:
            text = f.read()
        for m in re.finditer(r"# run \d+: screen x (-?\d+) y (-?\d+).*\n(?:#.*\n)*plane (\w)\nrow (.*)\n", text):
            runs.append((m.group(3), int(m.group(1)), int(m.group(2)), m.group(4).split()))
    return runs


class MdText(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.mkdtemp(prefix="chaotix_mdtext_")
        dump = os.path.join(cls.dir, "dump")
        run("--dump-graphics", dump)
        cls.runs = runs_in(dump)
        cls.plain = os.path.join(cls.dir, "plain")
        os.makedirs(cls.plain)
        run("--shot", SHOTS, "--out", cls.plain)

    def options(self):
        found = [r for r in self.runs if len(r[3]) == 7 and r[3][0] == r[3][4] and r[0] == "A"
                 and len(set(r[3])) == 6]
        self.assertTrue(found, "no OPTIONS run in the dump: %r" % self.runs)
        return found[0]

    def shots(self, mods, *extra):
        out_dir = tempfile.mkdtemp(dir=self.dir)
        out = run("--tiles", mods, "--shot", SHOTS, "--out", out_dir, *extra)
        return out, out_dir

    def mods(self, files):
        d = tempfile.mkdtemp(dir=self.dir)
        for name, content in files.items():
            if isinstance(content, str):
                with open(os.path.join(d, name), "w", encoding="utf-8") as f:
                    f.write(content)
            else:
                content(os.path.join(d, name))
        return d

    def frame(self, d, n):
        return os.path.join(d, "frame_%05d.png" % n)

    def same_files(self, a, b):
        for n in (TITLE, MENU):
            with open(self.frame(a, n), "rb") as x, open(self.frame(b, n), "rb") as y:
                self.assertEqual(x.read(), y.read(), "frame %d" % n)

    def changed_box(self, out_dir):
        """The bounding box of the pixels that differ from the plain menu."""
        a, b = read_png(self.frame(self.plain, MENU)), read_png(self.frame(out_dir, MENU))
        xs, ys = [], []
        for y, (ra, rb) in enumerate(zip(a, b)):
            for x, (pa, pb) in enumerate(zip(ra, rb)):
                if pa != pb:
                    xs.append(x)
                    ys.append(y)
        return (min(xs), min(ys), max(xs), max(ys)) if xs else None

    def test_the_dump_lists_the_menu(self):
        plane, x, y, tokens = self.options()
        self.assertTrue(all(re.fullmatch(r"[0-9a-f]{16}(:h|:v|:hv)?", t) for t in tokens), tokens)
        self.assertTrue(glob.glob(os.path.join(self.dir, "dump", "Text", "text_*.png")))

    def test_no_patterns_change_nothing(self):
        empty = self.mods({})
        out, d = self.shots(empty)
        self.assertIn("text: 0 pattern(s) loaded", out, out[-2000:])
        self.same_files(self.plain, d)
        # A pattern of tiles that are not on screen.
        nowhere = self.mods({"mdtext_nowhere.txt": "row 0123456789abcdef 0123456789abcdee\n",
                             "mdtext_nowhere.png": lambda p: solid(p, 16, 1)})
        out, d = self.shots(nowhere)
        self.assertIn("text: 1 pattern(s) loaded", out, out[-2000:])
        self.same_files(self.plain, d)

    def test_options_is_replaced_only_there(self):
        plane, x, y, tokens = self.options()
        mods = self.mods({"mdtext_options.txt": "# OPTIONS\nplane A\nrow %s\n" % " ".join(tokens),
                          "mdtext_options.png": lambda p: solid(p, 56, 1)})
        out, d = self.shots(mods, "--lockstep")
        self.assertIn("text: 1 pattern(s) loaded", out, out[-2000:])
        self.assertIn("bit-identical", out, out[-2000:])
        with open(self.frame(self.plain, TITLE), "rb") as a, open(self.frame(d, TITLE), "rb") as b:
            self.assertEqual(a.read(), b.read(), "the title before the menu")
        box = self.changed_box(d)
        self.assertIsNotNone(box, "the menu did not change")
        self.assertGreaterEqual(box[0], x)
        self.assertGreaterEqual(box[1], y)
        self.assertLess(box[2], x + 56)
        self.assertLess(box[3], y + 8)
        # Solid: every pixel of the run not covered by the 32X layer is one colour.
        rows = read_png(self.frame(d, MENU))
        colours = {rows[yy][xx] for yy in range(y, y + 8) for xx in range(x, x + 56)}
        self.assertLessEqual(len(colours), 4, colours)

        # The longest pattern wins: "OP" alone loses to OPTIONS over the same tiles.
        mods2 = self.mods({"mdtext_options.txt": "plane A\nrow %s\n" % " ".join(tokens),
                           "mdtext_options.png": lambda p: solid(p, 56, 1),
                           "mdtext_a_op.txt": "row %s\n" % " ".join(tokens[:2]),
                           "mdtext_a_op.png": lambda p: solid(p, 16, 2)})
        out, d2 = self.shots(mods2)
        self.assertIn("text: 2 pattern(s) loaded", out, out[-2000:])
        self.same_files(d, d2)

    def test_a_wider_image_reaches_past_the_run(self):
        plane, x, y, tokens = self.options()
        mods = self.mods({"mdtext_options.txt": "plane A\nalign center\nrow %s\n" % " ".join(tokens),
                          "mdtext_options.png": lambda p: solid(p, 72, 1)})
        out, d = self.shots(mods)
        box = self.changed_box(d)
        self.assertIsNotNone(box)
        self.assertLess(box[0], x)            # reaches left of the run
        self.assertGreaterEqual(box[0], x - 8)
        self.assertLessEqual(box[2], x + 56 + 7)
        self.assertLess(box[3], y + 8)

    def test_malformed_patterns_are_refused(self):
        plane, x, y, tokens = self.options()
        mods = self.mods({"mdtext_bad.txt": "row %s\nrow %s\n" % (" ".join(tokens), " ".join(tokens[:3])),
                          "mdtext_bad.png": lambda p: solid(p, 56, 1),
                          "mdtext_tall.txt": "row %s\n" % " ".join(tokens),
                          "mdtext_tall.png": lambda p: write_indexed_png(p, 56, 16, [[1] * 56] * 16, [(0, 0, 0), (9, 9, 9)])})
        out, d = self.shots(mods)
        self.assertIn("text: 0 pattern(s) loaded", out, out[-2000:])
        self.assertIn("mdtext_bad.txt: line 2: every row must have the same number of tiles", out)
        self.assertIn("mdtext_tall.txt: the image must be 8 pixels high per row of tiles", out)
        self.same_files(self.plain, d)


if __name__ == "__main__":
    unittest.main()
