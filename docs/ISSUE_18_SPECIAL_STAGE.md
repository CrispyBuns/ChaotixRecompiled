# Issue #18: the special stage has no widescreen

Reported as "the game stays in widescreen most of the time, but it glitches and
sticks to 4:3 when entering the special stages". By design it did: widescreen
was only switched on in scenes the level engine draws, and the special stage is
not one. This change gives it real widescreen.

## How the special stage draws

Game mode `0x20` (handler `$885850`). The tube is drawn by the master SH-2 as
polygons, from code the stage copies into SDRAM from ROM `0x7BE00` when it
starts. The game's own stage select reaches it as place 10; that branch of
`$8F72C8` (player to `$FFE048`, level to `$FFE01A`, 200 rings, mode `0x20`)
is now transcribed too, which is how the tests get there.

- Frame buffer: packed 8 bpp, 512-byte lines, the same layout as the levels.
- The rasterizer (`$06004858`) takes its bounding box and clips against a
  rectangle stored as data at SDRAM `0x06003844` (int16 left, right, top,
  bottom). The stage sets it to 16, 303, 4, 219: its picture is 288 px wide
  inside the 320 px screen. Other scenes use 0, 319, 0, 223.
- Spans are drawn with the 32X auto fill, edges with byte and word stores.
- Each frame the boot code at `$0600032C` clears lines 4-219 from x = 16 for
  145 words.

Widening the rectangle does make the tube's geometry reach into the margins,
but only to the right. The rasterizer turns 8.8 fixed-point x into pixels with
`SHLR8`, a logical shift, so a span that starts left of x = 0 gets a start
2^24 too large and a length to match; drawn, it swaps its ends and fills the
complement of the span. The game never draws left of x = 16, so it never meets
this.

## The repair

The tube is drawn C px further right, where x is never negative, and the host
moves it back (`runtime/patches.{h,cpp}`, `Machine::fb_write_special`, the
auto fill in `sh2_bus.cpp`):

- Every projection whose output goes to the polygon dispatcher (`$060045E8`)
  adds the screen centre as two `ADD #80,Rn`: the tube's vertex projections
  (`$06004442`, `$06004C5E`), the near-plane clippers (`$060044F4`,
  `$0600456C`) and a rotated one (`$0600414A`). Those immediates become
  80 + C/2. The add at `$06004BE0` places sprite objects (the player, the
  spheres), which are drawn with the overwrite image at native positions, so
  it stays at 160.
- The rectangle becomes [0, 319 + 2C], C being the margin rounded up to 8.
- Polygon pixels -- the rasterizer's stores and its fills, the frame buffer's
  only non-overwrite writes here -- are moved back by C. Anything outside the
  native 320 columns goes to the host-side margin shadow, as level sprites
  already do: the stage keeps data in the line padding.
- The clear is recognised by its exact fill (start word 8 of a line, length
  register `0x90`, data 0) and clears the whole widened line instead. Moved
  back like a polygon, it left the right 56 px of the picture uncleared, so
  old walls stayed where the native frame shows open space.

The code patch bumps `sdram_code_epoch`, so recompiled code covering those
bytes is revalidated and the interpreter runs the patched instructions. When
the stage ends (or widescreen is turned off) the original immediates and
rectangle are put back, unless the next scene has already replaced them.

## Validation

- `lockstep_special_stage_widescreen`: 1600 frames, interpreter and recompiled
  machine bit-identical with the patch running.
- `special_stage_is_widescreen`: the margins go live as the stage starts
  (frame 419, about 40 frames after the request is taken).
- `special_stage_centre_matches_4_3`: a 4:3 machine runs alongside; inside the
  stage's own rectangle every frame must match except along polygon edges.
  Polygons clipped at x = 16 / 303 natively are not clipped when widened, and
  a clipped edge can land a pixel apart: measured at most 1138 pixels (1.8%)
  per frame at E = 80, 1119 at E = 53. The check allows 2.5%. With the clear
  moved instead of recognised, it fails at once (several thousand pixels).
- The 68K work RAM is identical to a 4:3 run on every frame over 2400 frames,
  and the frame buffer swaps on the same frames: game logic and timing are
  unchanged. 407 fps headless in the widened stage against 472 in 4:3.
- The stage runs to its end ("TIME OUT / TOO BAD!", frame 12926 of a test run)
  in widescreen, and the margins let go the frame the next scene starts.

## Follow-up: issue #21

Two reports after 0.3.8, both reproduced:

- **Black lines.** The stage frames its 4:3 picture with black Mega Drive
  sprites (pattern `0x038`): 2x4-cell sprites at X 0 and 304 make 16 px
  columns down both sides, 4x1-cell ones make 4 px bars above and below.
  Widened, the columns stood in the middle of the picture, plainly visible in
  the stages whose tube leaves the starfield showing (stages 1 and 2). They
  are now not drawn while the stage is widened (`Vdp::hidden_frame_tile`;
  they still count for the sprite status flags, and do not cover sprites
  behind them), and the margins are black on the rows above and below the
  stage's rectangle, where the bars are.
- **Only from the stage select.** Only the stage select starts the special
  stage in game mode `0x20` (`$8F734A` is the only `move.w #$20,$FFDFDE` in
  the ROM). In play, a giant ring's object jumps to the stage handler from the
  level (`$891052 jmp $885850`) and the mode stays the level's `0x18`, so the
  check for mode `0x20` kept the stage 4:3. The stage is now recognised by its
  loaded code and its rectangle alone.

Tests: `special_stage_is_widescreen_in_the_level_mode` (the stage select's
stage with the mode set to `0x18` mid-stage must stay widened, in one stretch),
`widescreen_render.the_special_stage_is_found_whatever_the_game_mode`, and
`vdp_widescreen.a_hidden_frame_sprite_is_not_drawn_but_still_collides`.
Not done: a special stage reached through a real giant ring in a scripted run.
