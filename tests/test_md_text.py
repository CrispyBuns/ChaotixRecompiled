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

On DATA LOAD, switching slots changes the palette line of the slots' name
table entries during a picture; the slot's NEW (three rows of the 8x16
font's big letters), given a solid image, stays replaced in every picture
of the switch (0.7.0 showed the English NEW for one picture). The slots'
headers (NO SAVE, DATA 1: nine tiles of the panel's top three rows) are
cleared and written again during the pictures where the panel starts and
stops sliding, before the beam reaches them; they stay replaced in those
pictures too (0.7.1 fixed the English header shown for one picture).

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


def raw_rgb(path):
    """The headless shots' rows of RGB bytes (they are written unfiltered)."""
    with open(path, "rb") as f:
        data = f.read()
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        if kind == b"IHDR":
            w = struct.unpack(">I", data[pos + 8:pos + 12])[0]
            assert data[pos + 16:pos + 18] == bytes((8, 2)), "not 8-bit RGB"
        elif kind == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * 3 + 1
    assert all(raw[i] == 0 for i in range(0, len(raw), stride)), "filtered rows"
    return [raw[i + 1:i + stride] for i in range(0, len(raw), stride)]


def changed_pixels(a, b):
    n = 0
    for ra, rb in zip(raw_rgb(a), raw_rgb(b)):
        if ra != rb:
            n += sum(1 for i in range(0, len(ra), 3) if ra[i:i + 3] != rb[i:i + 3])
    return n


class DataLoadSlots(unittest.TestCase):
    PRESS = ["--press", "1800:start:6", "--press", "1950:start:6", "--press", "2150:down:6", "--press", "2300:up:6"]
    FIRST, LAST = 2140, 2400

    def run_slots(self, *args):
        r = subprocess.run([HEADLESS, "--rom", ROM, "--frames", str(self.LAST + 1), *self.PRESS, *args],
                           capture_output=True, text=True, timeout=900)
        return r.stdout + r.stderr

    def test_slot_headers_stay_replaced_while_the_panel_slides(self):
        d = tempfile.mkdtemp(prefix="chaotix_mdtext_headers_")
        self.run_slots("--frames", "2100", "--dump-graphics", os.path.join(d, "dump"))
        # A header: a run of 9 tiles under two rows of the panel's frame (the
        # same row of tiles, wider), the 9 tiles above it taken from those.
        headers = []
        for path in sorted(glob.glob(os.path.join(d, "dump", "Text", "text_*.txt"))):
            with open(path, encoding="utf-8") as f:
                runs = [(m.group(3), int(m.group(1)), int(m.group(2)), m.group(4).split()) for m in re.finditer(
                    r"# run \d+: screen x (-?\d+) y (-?\d+).*\n(?:#.*\n)*plane (\w)\nrow (.*)\n", f.read())]
            at = {(r[0], r[1], r[2]): r[3] for r in runs}
            for (plane, x, y), tokens in sorted(at.items(), key=lambda kv: kv[0][2]):
                if len(tokens) != 9:
                    continue
                for (p0, x0, y0), top in at.items():
                    mid = at.get((plane, x0, y - 8))
                    if p0 == plane and y0 == y - 16 and mid and len(mid) == len(top) and x0 <= x \
                            and x + 72 <= x0 + 8 * len(top):
                        k = (x - x0) // 8
                        headers.append([top[k:k + 9], mid[k:k + 9], tokens])
            if headers:
                break
        self.assertGreaterEqual(len(headers), 2, "no slot headers in the dump")
        mods = os.path.join(d, "mods")
        os.makedirs(mods)
        for i, rows in enumerate(headers):
            with open(os.path.join(mods, "mdtext_header%d.txt" % i), "w", encoding="utf-8") as f:
                f.write("".join("row %s\n" % " ".join(row) for row in rows))
            write_indexed_png(os.path.join(mods, "mdtext_header%d.png" % i), 72, 24, [[1] * 72] * 24,
                              [(0, 0, 0)] + [(255, 255, 255)] * 15)
        shots = ",".join(str(f) for f in range(self.FIRST, self.LAST + 1))
        plain, modded = os.path.join(d, "plain"), os.path.join(d, "modded")
        os.makedirs(plain)
        os.makedirs(modded)
        self.run_slots("--shot", shots, "--out", plain)
        out = self.run_slots("--tiles", mods, "--shot", shots, "--out", modded)
        self.assertIn("text: %d pattern(s) loaded" % len(headers), out, out[-2000:])
        # Matching again during the picture changes nothing for the machine.
        out = self.run_slots("--tiles", mods, "--lockstep")
        self.assertIn("bit-identical", out, out[-2000:])
        changed = []
        for f in range(self.FIRST, self.LAST + 1):
            name = "frame_%05d.png" % f
            changed.append(changed_pixels(os.path.join(plain, name), os.path.join(modded, name)))
        self.assertGreater(min(changed[:10]), 72 * 12, "the header is not replaced before the switch")
        # The headers slide in and out of view, and behind the banner: a
        # picture with far fewer replaced pixels than both of its neighbours
        # (a header's worth, of one or two on screen) showed a header
        # unreplaced for that one picture.
        dips = [(self.FIRST + i, changed[i - 1], changed[i], changed[i + 1]) for i in range(1, len(changed) - 1)
                if changed[i] < 0.6 * min(changed[i - 1], changed[i + 1])]
        self.assertEqual(dips, [], "pictures with a header not replaced (frame, changed before, in it, after)")

    def test_switching_slots_shows_no_unreplaced_picture(self):
        d = tempfile.mkdtemp(prefix="chaotix_mdtext_slots_")
        self.run_slots("--frames", "2100", "--dump-graphics", os.path.join(d, "dump"))
        # NEW: three runs of 7 tiles on one screen, each right below the one
        # before.
        new = None
        for path in sorted(glob.glob(os.path.join(d, "dump", "Text", "text_*.txt"))):
            with open(path, encoding="utf-8") as f:
                runs = [(m.group(3), int(m.group(1)), int(m.group(2)), m.group(4).split()) for m in re.finditer(
                    r"# run \d+: screen x (-?\d+) y (-?\d+).*\n(?:#.*\n)*plane (\w)\nrow (.*)\n", f.read())]
            at = {(r[0], r[1], r[2]): r[3] for r in runs if len(r[3]) == 7}
            for (plane, x, y), tokens in at.items():
                if (plane, x, y - 8) not in at and (plane, x, y + 8) in at and (plane, x, y + 16) in at \
                        and (plane, x, y + 24) not in at:
                    new = [tokens, at[(plane, x, y + 8)], at[(plane, x, y + 16)]]
                    break
            if new:
                break
        self.assertIsNotNone(new, "no NEW in the dump")
        mods = os.path.join(d, "mods")
        os.makedirs(mods)
        with open(os.path.join(mods, "mdtext_new.txt"), "w", encoding="utf-8") as f:
            f.write("".join("row %s\n" % " ".join(row) for row in new))
        write_indexed_png(os.path.join(mods, "mdtext_new.png"), 56, 24, [[1] * 56] * 24,
                          [(0, 0, 0)] + [(255, 255, 255)] * 15)
        shots = ",".join(str(f) for f in range(self.FIRST, self.LAST + 1))
        plain, modded = os.path.join(d, "plain"), os.path.join(d, "modded")
        os.makedirs(plain)
        os.makedirs(modded)
        self.run_slots("--shot", shots, "--out", plain)
        out = self.run_slots("--tiles", mods, "--shot", shots, "--out", modded)
        self.assertIn("text: 1 pattern(s) loaded", out, out[-2000:])
        # A NEW is on screen all along (the slot's, and the next slot's as
        # it slides in): each picture has at least one replaced in full.
        short = []
        for f in range(self.FIRST, self.LAST + 1):
            name = "frame_%05d.png" % f
            n = changed_pixels(os.path.join(plain, name), os.path.join(modded, name))
            if n < 56 * 24:
                short.append((f, n))
        self.assertEqual(short, [], "pictures with NEW not replaced (frame, changed pixels)")


if __name__ == "__main__":
    unittest.main()
