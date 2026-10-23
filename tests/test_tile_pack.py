#!/usr/bin/env python3
"""Mega Drive tile replacement with the real ROM (issue #35).

Dumps the tiles a level draws, then loads that dump, unchanged, as a
replacement pack: every tile is replaced by itself, so the picture must be
exactly the original one, and the recompiled machine must stay bit-identical
to the interpreter. Anything wrong in hashing, loading, palette handling or
drawing shows up as a difference.

usage: test_tile_pack.py HEADLESS ROM
"""
import glob
import os
import subprocess
import sys
import tempfile
import unittest

HEADLESS, ROM = sys.argv[1:3]
del sys.argv[1:3]

STAGE = ["--stage", "300:0:1:0:2:7:0", "--fuzz", "5:700:1200"]
SHOTS = "700,1000,1199"


def run(*args):
    r = subprocess.run([HEADLESS, "--rom", ROM, "--frames", "1200", *STAGE, *args],
                       capture_output=True, text=True, timeout=900)
    return r.stdout + r.stderr


class TilePack(unittest.TestCase):
    def test_a_pack_of_the_dumped_tiles_changes_nothing(self):
        d = tempfile.mkdtemp(prefix="chaotix_tiles_")
        dump, plain, packed = (os.path.join(d, n) for n in ("dump", "plain", "packed"))
        for p in (dump, plain, packed):
            os.makedirs(p)
        run("--dump-tiles", dump, "--shot", SHOTS, "--out", plain)
        tiles = glob.glob(os.path.join(dump, "md_*.png"))
        self.assertGreater(len(tiles), 100)

        out = run("--tiles", dump, "--shot", SHOTS, "--out", packed, "--lockstep")
        self.assertIn("tiles: %d replacement(s) loaded" % len(tiles), out, out[-2000:])
        self.assertIn("bit-identical", out, out[-2000:])
        for shot in sorted(glob.glob(os.path.join(plain, "*.png"))):
            other = os.path.join(packed, os.path.basename(shot))
            with open(shot, "rb") as a, open(other, "rb") as b:
                self.assertEqual(a.read(), b.read(), os.path.basename(shot))


if __name__ == "__main__":
    unittest.main()
