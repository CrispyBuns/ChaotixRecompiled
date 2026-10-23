#!/usr/bin/env python3
"""ROM patches with the real ROM (issue #35).

A patch is applied to the ROM in memory; the recompiled code was made from the
unpatched ROM. Data patches must leave everything native. A patch over code
must not be silently ignored: code run straight from ROM (the 68K's) switches
that CPU to the interpreter, and SH-2 code copied into SDRAM fails validation
and is interpreted. Either way the recompiled machine must stay bit-identical
to the interpreter (lockstep).

usage: test_rom_patches.py HEADLESS ROM RECOMP_TABLES_CPP
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest

HEADLESS, ROM, TABLES = sys.argv[1:4]
del sys.argv[1:4]


def ips(records):
    out = b"PATCH"
    for off, data in records:
        out += off.to_bytes(3, "big") + len(data).to_bytes(2, "big") + data
    return out + b"EOF"


def tables():
    with open(TABLES, encoding="utf-8") as f:
        text = f.read()
    rom_code = [(int(c), int(o, 16), int(n)) for c, o, n in re.findall(r"\{(\d), 0x([0-9A-F]+)u, (\d+)\}", text)]
    start = text.index("g_sh2_ranges[]")
    sh2 = [(int(a, 16), int(n), int(o, 16))
           for a, n, o in re.findall(r"\{0x([0-9A-F]+)u, (\d+), 0x([0-9A-F]+)u\}", text[start:text.index("};", start)])]
    return rom_code, sh2


class RomPatches(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(ROM, "rb") as f:
            cls.rom = f.read()
        cls.rom_code, cls.sh2 = tables()
        cls.dir = tempfile.mkdtemp(prefix="chaotix_patch_")

    def run_patched(self, name, records, frames=900):
        path = os.path.join(self.dir, name + ".ips")
        with open(path, "wb") as f:
            f.write(ips(records))
        r = subprocess.run([HEADLESS, "--rom", ROM, "--frames", str(frames), "--lockstep", "--patch", path],
                           capture_output=True, text=True, timeout=600)
        out = r.stdout + r.stderr
        self.assertIn("patch: 1 applied", out, out[-2000:])
        self.assertIn("bit-identical", out, out[-2000:])
        return out

    def flipped(self, off, n=2):
        return bytes(b ^ 0xFF for b in self.rom[off:off + n])

    def test_a_data_patch_keeps_the_recompiled_code(self):
        # Two letters of the header's domestic name: data nothing runs.
        out = self.run_patched("data", [(0x120, b"KN")])
        self.assertNotIn("interpreted (a ROM patch", out)
        self.assertRegex(out, r"68K native \d+ / interp \d+ \((9\d|100)\.\d% native\)")

    def test_a_patch_over_68k_code_uses_the_interpreter(self):
        cpu, off, n = [r for r in self.rom_code if r[0] == 0][-1]   # 68K code the boot does not reach
        out = self.run_patched("m68k", [(off, self.flipped(off))])
        self.assertIn("68K interpreted (a ROM patch changes its code)", out)

    def test_a_patch_over_sh2_code_fails_its_validation(self):
        # The SH-2 boot code is copied from ROM into SDRAM and always runs.
        addr, n, off = next(r for r in self.sh2 if r[0] == 0x06000884)
        out = self.run_patched("sh2", [(off + 2, self.flipped(off + 2))], frames=120)
        m = re.search(r"invalid (\d+)\)", out)
        self.assertTrue(m and int(m.group(1)) > 0, out[-2000:])


if __name__ == "__main__":
    unittest.main()
