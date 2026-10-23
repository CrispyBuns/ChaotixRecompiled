# Issue #36: Marina Madness's background stops at the 4:3 edges

Reported with a 1920x1080 screenshot: in widescreen, MARINA MADNESS's band of
clouds and sea ends exactly where a 4:3 picture would, while the level and the
sky above the band carry on into the margins. Every act and every time of day
looks the same.

## What was wrong

Reproduced with `chaotix_headless --stage 300:4:1:0:2:7:0 --press
900:right:1600 --wide 53 --shot 1800` (`--layer 32x` shows the 32X layer on
its own).

The band is not part of a Mega Drive plane. It is drawn on the 32X layer, by
four objects that stay put on the screen at X 160 (Y 90, 125, 142 and 159):
the clouds and three strips of sea below them. `--sprites-log` lists them as
bank 34, frames 0-3. Each object's mapping (68K 0x9D944C-0x9D946B, in the ROM's
banked window) has two pieces, the same frame at X - h and X + h:

| frame | what | h | the two copies cover |
| --- | --- | --- | --- |
| 0 | clouds | 96 | -32 .. 352 |
| 1-3 | sea | 80 | 0 .. 320 |

The two copies meet in the middle, so the picture is made to repeat every 2h,
but the game only ever puts down two of them. The widened clip rectangle lets
the 32X draw out to the margins; there was just nothing there to draw, and the
sky gradient behind the band showed instead (the clouds reached a little past
the 4:3 edges, the sea not at all).

No other zone draws its backdrop this way. (A draw-list scan of every
zone's first act, and all MARINA MADNESS acts, at every time of day found
one other repeated 32X picture, in BOTANIC BASE. It is put down four times,
160 px apart, which already reaches past any margin.)

## The repair

A 68K hook at the end of the routine that pushes an object's pieces onto the 32X
command list (`patches::kPiecesDone`, 0x88126E, the `rts` after the last
piece). In a widened level, if the object is one of those four mappings, the
hook pushes more copies of the object's last command, 2h apart, to the left of
the left piece and to the right of the right piece, until the clip rectangle
(`-C .. 320 + C`) is covered. That means one more copy each side, at every
supported width. The copies are the game's own `op 0x01` commands with only X
changed, so the SH-2 draws them with the same blitter, clip and transparency.

The hook is cautious about what it extends. The mapping is recognised by its
address and checked by its bytes: two pieces on the same line, the same
frame, at -h and +h, no flags. The hook also checks that both pieces were just
pushed and are 2h apart. It never takes the list below the point where the game
stops adding objects (`$D0A0`, checked at 0x8811CA).

Nothing changes without margins. The hook only acts while `wide_active` is set
(and not in the special stage), so a 4:3 window gets the same command list,
byte for byte. Like the other 68K hooks, the reference interpreter and the
recompiled code call it before the same instruction, and it only depends on
machine state, so lockstep holds.

## Validation

- `widescreen_marina_madness_backdrop_fills_the_margins` (new): the draw list
  of a widened MARINA MADNESS frame must have the extra copies, clouds at X -128
  and 448, and sea at -80 and 400. It fails without the repair.
- `lockstep_widescreen_marina_madness` (new): 2000 frames of act 2 at sunset,
  widened, with fuzzed input, bit-identical between the interpreter and the
  recompiled code.
- Margins of 0, 53, 64 and 80 px over the repro run, shots at frames 600, 1200
  and 1800, before and after:
  - Without margins the frames are byte-identical.
  - With margins, every changed pixel is in the margins, inside the band's rows;
    the 320 native columns are pixel-identical.
  - With 80 px margins the clouds and sea reach both edges of the picture with
    no seam.
- The 33-configuration lockstep sweep (every special stage, levels with
  `--hud-edges` including three MARINA MADNESS acts, the bonus stage, the hub
  and the level-mode special stage), with fuzzed input over 3000 frames, is
  bit-identical throughout.
- The full `ctest` suite passes, including `widescreen_centre_matches_4_3`,
  `golden_frames_widescreen` and `golden_frames_16_9`.
