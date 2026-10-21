# How the 32X layer is drawn

Research for issue #35 (replacing the 32X layer's graphics: characters, the
HUD, objects, the title). The [tile replacement](TEXTURE_MODS.md) covers the
Mega Drive layer only. These notes say how the 32X layer is drawn, as found by
attributing frame buffer writes to SH-2 PCs and by instruction traces. Scenes
checked: Botanic Base 1 (with Knuckles and Espio both ways round), Speed
Slider 1, the hub, the bonus stage, the special stage, the SEGA logo, the
title intro and the title screen. Addresses are SH-2 addresses; the SH-2 code
runs in SDRAM, copied from ROM 0x77800.

## Who draws

- **Only the master SH-2** writes the frame buffer. The slave runs code from
  its cache (probably the PWM sound) and writes no frame buffer byte.
- **Every sprite pixel** comes from one of two blitters, both writing through
  the overwrite image (0x24020000+), where zero bytes are transparent:
  - **0x06001380, unscaled.** Twelve variants are chosen by the flags through
    the `braf` table at 0x060013AC.
  - **0x06001F18, scaled.** It uses the hardware divider.
- **Direct writes** come only from the polygon filler (0x06004792–0x06004852,
  reached through the dispatcher 0x060045E8): the bonus stage, the title
  intro and the special stage's tube. The per-frame clear is an auto fill.

## The command list

The 68K builds one command list a frame and sends it through the 32X FIFO.
The master copies it with DMA into one of two SDRAM buffers, 0x06004B10 and
0x06004F10, which swap each frame (pointers at 0x06003814/0x06003818). The
interpreter at 0x06000984 reads opcode bytes and dispatches through the table
at 0x060009B4. Opcodes run from 0x00 (end of frame) to 0x1A. The ones seen
drawing:

| op | address | use |
| --- | --- | --- |
| 0x01 | 0x06000A50 | a sprite: characters and objects in levels, the hub, the title, the SEGA logo |
| 0x11 | 0x06000C9C | the HUD: labels from a layout table at 0x06000680, digits one glyph per call |
| 0x12 | 0x06000CD0 | a projected object (the bonus stage): flat polygons, then a scaled sprite |
| 0x0A | 0x06000B7A | polygons only |

The special stage has its own SDRAM code, which calls the same two blitters
for the player and the spheres.

**op 0x01 is 12 bytes:** `01 BANK FRAME FLAGS X.w Y.w SX.w SY.w`.

- X and Y are the sprite's hotspot in screen pixels.
- SX and SY are 8.8 scale factors; 0x100 is unscaled, and anything else goes
  to the scaled blitter.
- **Banks above 15:** the graphic is `[[0x06003614 + 4·BANK] + 4·FRAME]`, in
  ROM or SDRAM.
- **Banks 0–15 are the characters** (0 Mighty, 2 Knuckles, 3 Charmy, 4 Vector,
  5 Bomb, 6 Heavy, 7 Espio; BANK+8 is the same character's second set). Their
  graphic is copied by 0x06000810 into the scratch buffer 0x06005310, adding a
  colour offset to every non-zero pixel. The offset is 0x12 for the player's
  character and 0x24 for the partner (set by 0x06000F42; verified by swapping
  the two).

**Blitter registers:**

- 0x06001380 takes r1 = X, r2 = Y, r3 = flags, r4 = graphic. It clips against
  the rectangle at 0x06003834.
- 0x06001F18 also takes r5 = SX and r6 = SY, which must lie within the range
  at 0x06003840.

**Flags:**

- bit 0: horizontal flip.
- bit 1: vertical flip.
- bit 2: subtract 0x80 from each non-zero pixel (the other palette half).
- bit 3: every second pixel column only, a stipple transparency.

## The graphics

Graphics are 8 bpp palette indices, the same format in ROM and in SDRAM:

1. A header: `int16 x0, x1` is the box in pixels relative to the hotspot, and
   `int16 y0, y1` is the box in lines × 256.
2. Spans, each `int8 xs, int8 xe, int16 y` (lines × 256) followed by `xe − xs`
   pixel bytes, where 0 is transparent.
3. A span with `xs == xe` ends the graphic.

Each scene copies the bank table (128 entries, from ROM 0x020A0000) to
0x06003614, then decompresses chosen banks into SDRAM (0x06000464, with the
bit-stream decompressor at 0x0600075A/0x06000856) and points their entries at
the copies. So characters are drawn from ROM, while many object, HUD (the font
is bank 0x13) and title banks are drawn from SDRAM, at addresses that depend
on the scene.

The frame buffer is never read back (no SH-2 or 68K reads in the scenes
checked), the line table is standard, and the shift register is 0.

## Identifying a graphic

The stable key is **(BANK, FRAME)**: a bank is a fixed slot of the ROM's
table, and only where it is stored changes. Source addresses are stable only
for banks drawn from ROM; for example Knuckles' frames come from the same ROM
addresses in every level, the hub, and as partner. Flags, scale and the
colour offset are drawing parameters, not part of the key. A hash of the
graphic's data could identify graphics from a patched ROM.

One call draws one whole graphic, but some objects are several graphics:

- Espio is two or three calls a frame at one hotspot.
- The title's CHAOTIX logo is four pieces, its group of characters is two
  halves, and its pillars are one graphic drawn twice, once flipped.
- The HUD is one call per label and one per digit.

## What makes replacement hard

- **Palette arithmetic:** the colour offset, and flag bit 2, mean one graphic
  is shown in different palette ranges.
- **Priority per palette entry:** each palette entry has its own priority
  bit, which decides per pixel whether the 32X shows over the Mega Drive. It
  is set on 123 of the 256 entries in a level. A replacement needs a
  priority.
- **Scaling** exists: op 0x01 with SX/SY ≠ 0x100, op 0x12, the title intro and
  the special stage. No rotation was found.
- **Painter's order:** sprites are drawn in list order into a buffer cleared
  each frame, so swapping one graphic in place leaves holes where it covered
  another. The layer has to be rebuilt in order.
- **The scratch buffer:** every character is drawn from 0x06005310, so the
  blitter's r4 does not identify it.
- **Double buffering:** draws go to the back buffer, and the bonus stage
  flips only every second frame. A draw list belongs to a buffer, not a
  frame.
- **Widescreen** moves the clip rectangle, so positions can lie outside
  0..319.
- **Polygons** (op 0x0A, op 0x12, the tube) land in the same layer.

## Plan

1. **Record draws.** Add record-only SH-2 PC hooks, the counterpart of the
   68K's patch hooks, called by both the interpreter and the generated code so
   lockstep is unaffected, at:
   - 0x06001380 (r1–r4);
   - 0x06001F18 (r1–r6);
   - 0x06000810 (r1, the ROM source, and r3, the colour offset), for the
     draws whose r4 is the scratch buffer.

   Map each graphic pointer back to (BANK, FRAME) with a host-side index of
   the bank table, rebuilt when the table changes. Keep one draw list per
   frame buffer, and publish it when the display flips.
2. **Self-test.** Rebuild the displayed buffer's 32X layer host-side from the
   list and the graphics' own data. With no replacements it must equal the
   emulated frame buffer exactly; this runs headless over the lockstep
   sessions. Pixels the list does not explain (polygons) come from the
   emulated frame buffer.
3. **Replace.** Draw a PNG in place of a graphic at its hotspot, flipped and
   scaled as the original, with the original's priorities. High-resolution
   PNGs need the compositor to output more than 320 pixels, which is a
   separate step.

Not determined yet: which 68K routine builds the list, the layouts of the
other opcodes, the exact vertical-flip offset, and whether all of the special
stage's graphics are in the bank table.
