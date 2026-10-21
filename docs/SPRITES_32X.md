# How the 32X layer is drawn

Research for issue #35 (replacing the 32X layer's graphics: characters, the
HUD, objects, the title). The [tile replacement](TEXTURE_MODS.md) covers the
Mega Drive layer only. These notes say how the 32X layer is drawn, as found by
attributing frame buffer writes to SH-2 PCs and by instruction traces. Scenes
checked: Botanic Base 1 (with Knuckles and Espio both ways round), Speed
Slider 1, the hub, the bonus stage, the special stage, the SEGA logo, the
title intro and the title screen. Addresses are SH-2 addresses; the SH-2 code
runs in SDRAM, copied from ROM 0x77800.

Steps 1 and 2 of the plan below are done (`runtime/sprite_record.*`): the
draws are recorded, and the layer's sprites can be rebuilt host-side exactly.
Everything marked *verified* was checked by that rebuild against the
emulated frame buffer, and against the game's own blitter code run on random
calls.

## Who draws

- **Only the master SH-2** writes the frame buffer. The slave runs code from
  its cache (probably the PWM sound) and writes no frame buffer byte.
- **Every sprite pixel** comes from one of three blitters, all writing through
  the overwrite image (0x24020000+), where zero bytes are transparent
  (*verified*: no other code writes the overwrite image in the scenes below):
  - **0x06001380, unscaled.** Twelve variants are chosen by the flags through
    the `braf` table at 0x060013AC; flags 12-15 run past its end.
  - **0x06001F18, scaled.** It uses the hardware divider.
  - **0x060023EC, scaled with a sine-shaped stretch** (op 0x10; the waving
    SEGA logo, bank 48). It shares the scaled blitter's drawing code at
    0x06002166.
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
| 0x00 | 0x060009EA | end of the list; first draws the sprites listed at 0x0600D310 (a menu's), then polygons |
| 0x01 | 0x06000A50 | a sprite: characters and objects in levels, the hub, the title, the SEGA logo |
| 0x10 | 0x06000C48 | a stretched sprite (0x060023EC): the SEGA logo |
| 0x11 | 0x06000C9C | the HUD: labels from a layout table at 0x06000680, digits one glyph per call |
| 0x12 | 0x06000CD0 | a projected object (the bonus stage): flat polygons, then a scaled sprite |
| 0x0A | 0x06000B7A | polygons only |

The special stage has its own SDRAM code, which calls the same blitters for
the player and the spheres; in play its graphics are all in the bank table
(banks 19, 39, 79, 82).

**op 0x01 is 12 bytes:** `01 BANK FRAME FLAGS X.w Y.w SX.w SY.w`.

- X and Y are the sprite's hotspot in screen pixels.
- SX and SY are 8.8 scale factors; 0x100 is unscaled, and anything else goes
  to the scaled blitter.
- **Banks above 15:** the graphic is `[[0x06003614 + 4·BANK] + 4·FRAME]`, in
  ROM or SDRAM.
- **Banks 0–15 are the characters** (0 Mighty, 2 Knuckles, 3 Charmy, 4 Vector,
  5 Bomb, 6 Heavy, 7 Espio; BANK+8 is the same character's second set). Their
  graphic is copied by 0x06000810 into the scratch buffer 0x06005310, adding a
  colour offset to every non-zero pixel. The offset is the byte at
  0x06004B00 + BANK: 0x12 for the player's character and 0x24 for the partner
  (set by 0x06000F42; verified by swapping the two). The addition wraps, so a
  pixel can become 0 and transparent.

**op 0x10 is 24 bytes:** `10 BANK FRAME FLAGS X.w Y.w` and eight words, four
per axis: P0 the scale (8.8, as SX), P1 and P2 the sine's phase at the first
and last column, P3 its amplitude (below).

The sprites op 0x00 draws come from a list at 0x0600D310 (graphic, X, Y,
flags). The routine at 0x06002C0A runs before each, and can change the
graphic's copy first; so a copy is drawn as the bytes it holds when it is
drawn, which the recorder checks.

**Blitter registers:**

- 0x06001380 takes r1 = X, r2 = Y, r3 = flags, r4 = graphic. It clips against
  the rectangle at 0x06003834.
- 0x06001F18 also takes r5 = SX and r6 = SY, which must lie within the range
  at 0x06003840, or nothing is drawn.
- 0x060023EC takes r1–r4, and r5 pointing to the eight words of op 0x10.
- 0x06000810 (the recolour copy) takes r1 = source, r2 = destination,
  r3 = offset.

**Flags:**

- bit 0: horizontal flip. Source column s lands on X − 1 − s (*verified*, all
  three blitters).
- bit 1: vertical flip. Source line l lands on Y − 1 − l (*verified*; the
  unscaled blitter moves its base one line up and negates the lines).
- bit 2: the other palette half. The unscaled blitter subtracts 0x80 from
  each non-zero pixel; the scaled ones OR in 0x80. The same for the game's
  pixels, which are all below 0x80.
- bit 3: every second pixel column only, a stipple transparency: the even
  source pixels of each span, or for an h-flip the odd ones, so both land on
  the same screen columns. Unscaled blitter only.

## The graphics

Graphics are 8 bpp palette indices, the same format in ROM and in SDRAM:

1. A header: `int16 x0, x1` is the box in pixels relative to the hotspot, and
   `int16 y0, y1` is the box in lines × 256.
2. Spans, each `int8 xs, int8 xe, int16 y` (lines × 256) followed by `xe − xs`
   pixel bytes, where 0 is transparent. The count is even: the blitters copy
   words.
3. A span with `xs == xe` ends the graphic.

Each scene copies the bank table (128 entries, from ROM 0x020A0000) to
0x06003614, then decompresses chosen banks into SDRAM (0x06000464, with the
bit-stream decompressor at 0x0600075A/0x06000856) and points their entries at
the copies. So characters are drawn from ROM, while many object, HUD (the font
is bank 0x13) and title banks are drawn from SDRAM, at addresses that depend
on the scene.

The frame buffer is never read back (no SH-2 or 68K reads in the scenes
checked), the line table is standard, and the shift register is 0.

## How the blitters draw (verified)

**Clipping.** The rectangle at 0x06003834 is `int16 left, right` (pixels,
right exclusive) and `int32 top, bottom` (lines × 256, bottom inclusive). The
unscaled blitter tests the graphic's box first: outside, nothing; wholly
inside, every span is copied as it is, even one the box does not hold;
across an edge, each span is clipped (to the line, and to the columns). For a
stipple at an odd X the column window moves right by one, which changes
nothing visible.

**Odd hotspots.** The unscaled blitter stores 16-bit words. At an odd X each
store holds the last pixel of one source word and the first of the next; it
builds them from `mov.w` loads, which sign-extend, so a pixel of 0x80 or more
sets the neighbouring byte to 0xFF. The game's graphics never trigger this.

**Scaling (0x06001F18).** The step is `0x01000000 / S` from the divider
(signed, truncated; 0x100 gives 0x10000). Positions are 16.16 and every
product wraps at 32 bits. Source column s starts at
`floor(((X << 16) + s · step) / 65536)` and covers up to where column s + 1
starts, clipped to [left, right); flipped, positions run leftwards from
`(X << 16) − 1`. Lines go the same way from `Y << 16`, clipped to
[top, bottom + 1); flipped, upwards from `Y << 16` with no −1. The box's
column count is `x1 + 1 − x0` and only the high bytes of the y words are
read. For example at S = 0xC0 the step is 0x15555, and the fourth column
starts at 103, not 104 (three steps are 0x3FFFF). Each source pixel then
fills its rectangle with 16-bit stores, ORing neighbouring pixels into one
word; with `mov.b` sign-extending, a pixel of 0x80 or more fills its word with
0xFF where it is doubled. Again the game's scaled graphics stay below 0x80.

**The stretch (0x060023EC).** Per axis, from P0–P3 (subroutine 0x060025F2):
the start is `(x0 << 8) / P0` and the size `(n << 8) / P0` for n columns
(`x1 − x0`; lines `y1 + 1 − y0`); each column is `0x100 + (P3 · sin) >> 8`
wide, the sine (int16, table at 0x06003C48) indexed by a phase running from
P1 to P2 in 8.8 (its step a 16-step DIV1 division), and the widths are then
scaled to add up to the size with a 16.16 sum starting at 0x7F00. Positions
are whole pixels, clipped like the scaled blitter's but with an inclusive
bottom. With P0 = 0x100 and P3 = 0 it is the unscaled picture.

## Identifying a graphic

The stable key is **(BANK, FRAME)**: a bank is a fixed slot of the ROM's
table, and only where it is stored changes. Source addresses are stable only
for banks drawn from ROM; for example Knuckles' frames come from the same ROM
addresses in every level, the hub, and as partner. Flags, scale and the
colour offset are drawing parameters, not part of the key. A hash of the
graphic's data could identify graphics from a patched ROM.

The recorder finds a call's (BANK, FRAME) from its graphic address with a
reverse index of the bank table: a frame table has no length, so it is taken
to end where the next one starts, at the first graphic it points past, or at
the first entry that is not a graphic's address; where two slots hold the
same graphic the lower one names it. The index is rebuilt when the table
changes, and at most once a frame when an address is not found. In the
scenes above 0.5–6% of the calls are not in the table (title and menu
graphics, the lobby's), and the lobby draws with a few garbage addresses far
off screen; those are named by a hash of their bytes only.

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
- **Scaling** exists: op 0x01 with SX/SY ≠ 0x100, op 0x10, op 0x12, the title
  intro and the special stage. No rotation was found.
- **Painter's order:** sprites are drawn in list order into a buffer cleared
  each frame, so swapping one graphic in place leaves holes where it covered
  another. The layer has to be rebuilt in order.
- **The scratch buffer:** every character is drawn from 0x06005310, so the
  blitter's r4 does not identify it.
- **Double buffering:** draws go to the back buffer, and the bonus stage
  flips only every second frame. A draw list belongs to a buffer, not a
  frame.
- **Widescreen** moves the clip rectangle, so positions can lie outside
  0..319, and the pixels there go to the host-side margin shadow.
- **Polygons** (op 0x0A, op 0x12, the tube) land in the same layer, and can
  cover sprites drawn before them.

## Recording and rebuilding (steps 1 and 2)

Record-only SH-2 hooks (`patches::kSh2Hooks`: 0x06000810, 0x06001380,
0x06001F18, 0x060023EC) run before those instructions on the master SH-2, in
the interpreter and in the generated code alike, only while a recorder is
installed (`Machine::sprites`). They read registers and memory and write
nothing the machine sees; the recompiler emits the call before the
instruction and skips its closed-form loop shortcuts in a hooked block, so
cycles and block boundaries are unchanged. The recorder checks each routine's
first instruction, so other code at those addresses is not taken for it.

Each call becomes a `SpriteDraw` in the draw list of the back buffer: graphic,
hotspot, flags, scale, the colour offset and source of a character, the clip
rectangle and scale limits at the time, (BANK, FRAME), a hash, and a copy of
the graphic's bytes (SDRAM graphics can be replaced before the picture is
shown). When the display flips, the shown buffer's list is complete and the
new back buffer's starts again.

`rebuild_sprites()` draws a list host-side into an image of the frame buffer
(0x20000 bytes, indexed like it), in list order, the way the blitters do.
That is the path a replacement will take.

`chaotix_headless` options:

- `--sprites-log FRAME[,FRAME...]`: the displayed buffer's list at those
  frames, one line per call (bank, frame, x, y, flags, scale, colour offset,
  source, size, hash, the line the call was made on).
- `--sprites-check`: every new picture rebuilt and compared with the emulated
  frame buffer. A host-side record of who wrote each byte last (the blitters'
  overwrite writes against direct writes and fills) leaves out bytes polygons
  or fills drew over; the rest must match exactly.
- `--sprites-oracle N[:SEED]`: N random calls of the three blitters, run by
  the game's own code on the interpreter and by the rebuild; they must write
  the same bytes. Graphics with pixels of every value, spans outside their
  box (plain blitter), any flags, clip rectangles and scales.
- `--sprites`: record only (for timing).

With `--lockstep` the reference machine records too, and the two draw lists
must be identical every frame.

Results, 3000 frames each with random input (`--fuzz`), widescreen 53 and
4:3: Botanic Base 1, the lobby, the bonus stage, the title (to its menus),
the special stage, and Speed Slider, Amazing Arena, Techno Tower, Marina
Madness and a second special stage — every picture rebuilt with **0
mismatching bytes** (1500–2800 pictures and 14–70 million compared bytes a
run). The oracle agrees on 20000 random calls. Lockstep stays bit-identical
with the recorder on both machines. The recorder costs about 2% of the frame
time (0.03 ms a frame); not installed, nothing measurable.

Found on the way: in a widened level, *any* auto fill word that lands on a
line's first column also clears that line's margins in the shadow
(`Machine::fb_margin_clear_line`), not only the per-frame clear; in Marina
Madness a fill from column 0 so erases sprite pixels already drawn in the
margins. The self-test counts those bytes as drawn over.

## Plan

1. **Record draws.** Done (above).
2. **Self-test.** Done (above).
3. **Replace.** Draw a PNG in place of a graphic at its hotspot, flipped and
   scaled as the original, with the original's priorities, as part of the
   rebuild; bytes the list does not explain (polygons, fills) come from the
   emulated frame buffer. High-resolution PNGs need the compositor to output
   more than 320 pixels, which is a separate step.

Not determined yet: which 68K routine builds the list, the layouts of the
other opcodes, and what the routine at 0x06002C0A does to the menu's copies.
