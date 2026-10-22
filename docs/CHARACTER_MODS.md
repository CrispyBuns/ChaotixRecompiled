# Character mods

A character mod gives one of the game's characters new colours and new
handling: Mighty turned into a fast blue hedgehog, Charmy into a fox with
two tails. It is one text file per character in `Mods/Characters`, with up
to three sections:

- `[Palette]`: the character's 18 colours;
- `[Cycle]`: Espio's colour cycle (his shimmer);
- `[Stats]`: how the character moves: acceleration, top speed, gravity,
  jump, and more.

New sprites, the other half of a new character, are PNG files in
`Mods/Textures` ([TEXTURE_MODS.md](TEXTURE_MODS.md)); the two combine (see
the end). Where all of this lives in the ROM, and how it was found, is in
[CHARACTERS.md](CHARACTERS.md).

## The files

`Mods/Characters` is created, with a short README, the first time the game
runs (where `Mods` is: see the README's "Mods" section). The files are named
after the character, in any case:

| file | character |
| --- | --- |
| `mighty.ini` | Mighty |
| `knuckles.ini` | Knuckles |
| `charmy.ini` | Charmy |
| `vector.ini` | Vector |
| `bomb.ini` | Bomb |
| `heavy.ini` | Heavy |
| `espio.ini` | Espio |

The game's table has an eighth, unused slot (between Mighty and Knuckles);
it shares Mighty's colours and movement record and has no sprites of its own,
so it takes no file (`slot1.ini` is reported and ignored).

The format is an INI file. `;` starts a comment anywhere on a line, `#` at
the start of one. Section names and keys are not case-sensitive. Each
section is optional, and so is each line in it: what a file does not give
stays as the game has it. A line the game cannot use is skipped and said in
the log (`characters:`), with the file name and line number.

```ini
; mighty.ini: a blue Mighty that runs and jumps further.
[Palette]
2 = #000073
3 = #0000E7
4 = #4A4AE7
5 = #7B7BEF

[Stats]
acceleration = 0x10
jump = 0x700
```

## Colours: `[Palette]`

A character's pixels have the values 1 to 18 (0 is transparent). In a level
or the lobby the player's character is drawn with 32X palette entries
0x13-0x24 and the partner with 0x25-0x36, so pixel value `p` is entry
`0x12 + p` or `0x24 + p`; the key is `p`:

```ini
[Palette]
1 = #080008
2 = #31006B
18 = #E72100
```

**Which pixel is which.** Turn on DUMP GRAPHICS and look at a dumped
character sprite (`Mods/Textures/Dump/32x_<bank>_<frame>.png`, bank 0 Mighty,
2 Knuckles, 3 Charmy, 4 Vector, 5 Bomb, 6 Heavy, 7 Espio): its palette
indices are the pixel values, so index 3 is key `3`. The colours each
character has in the ROM are listed by `[Palette]` order in an "identity"
file, which the test `tests/test_character_mods.py` can write for you
(`identity()`), or read from the ROM at the addresses in CHARACTERS.md.

**Colour values.** `#RRGGBB` as in any paint program, or a 32X colour as the
hardware stores it, `0x0000`-`0x7FFF` (`$` also works): bits 0-4 red, 5-9
green, 10-14 blue. The 32X has 32 levels per component, so `#RRGGBB` is
rounded to the nearest: each component `v` becomes `(v * 31 + 127) / 255`,
and is shown as `(c << 3) | (c >> 2)`. So `#FF0000` is `0x001F`, and every
colour the game can show comes back as itself (`#E70000` is red 28). Bit 15,
the priority bit, is not part of a colour: it decides per entry whether the
32X shows in front of the Mega Drive layer, and stays the game's.

**What it changes.** Only the picture. The game's palette (CRAM) is never
touched; when a line of the 32X layer is shown, a copy of it is used with
that character's entries replaced. The game's fades, which work on CRAM,
are followed: while the character's entries hold its own colours the mod's
are shown, and while they hold a step of a fade-out (every component lower
by the same `n`, down to 0) or a fade-in (every component at most `n`), the
mod's colours are shown faded by the same step. Anything else in those
entries (another scene's colours) is shown as it is. Which entries are the
character's comes from the game itself (each character's colour offset, as
the game sets it for its sprites), and the entries' colours must be that
character's: so a mod colours exactly the character it is for, as player or
partner, swapped or not, and nothing else. The copies at 0x93-0xB6 (the other
palette half, which some draws use) follow too.

Because nothing emulated changes, the recompiled code stays bit-identical to
the interpreter with a palette loaded, and colours can be changed while the
game runs: they are read again whenever the Mods folder is (at start, and on
Android when the app comes back to the front).

## Espio's shimmer: `[Cycle]`

Espio's pixels 2-6 are not fixed: the game cycles them through 16 sets of
five colours (ROM `0x0E7BA`), which is his shimmer. A recoloured Espio sets
them too:

```ini
[Cycle]
0 = #31006B #8400CE #B510EF #C639EF #F77BFF
1 = #31007B #6B00D6 #8C21EF #944AEF #C68CFF
; ... up to 15
```

The key is the set, 0 to 15; the value five colours, for pixels 2, 3, 4, 5
and 6. Set 0 is the colours Espio is loaded with. For a set the file does
not give, each of the five pixels shows the file's `[Palette]` colour for it
if it has one, otherwise Espio's own cycle colour: so a file that gives
pixels 2-6 in `[Palette]` and no `[Cycle]` makes a character that does not
shimmer, and one that changes only other pixels keeps Espio's shimmer.

## Handling: `[Stats]`

Each character has a 0x4A-byte record in the ROM's movement table
(`$88BE58`) that the shared player code reads every frame. `[Stats]` sets
fields of it by name. Values are the game's own units: decimal, or hex as
`0x...` or `$...`. "x 256" means 256 is one pixel per frame (8.8 fixed
point); 16.16 means 65536 is.

| name | size | meaning | Mighty | Knuckles | Charmy | Vector | Bomb | Heavy | Espio |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `speed_cap` | word | top X and Y speed, x 256 (ground speed itself has none) | 0x1000 | 0x0E00 | 0x0C00 | 0x0C00 | 0x0800 | 0x0600 | 0x1000 |
| `acceleration` | word | ground speed gained per frame, x 256; also the slowing down with nothing pressed | 0x0C | 0x0A | 0x0E | 0x07 | 0x0C | 0x04 | 0x0C |
| `deceleration` | word | slowing down when pressing against the motion (skid), x 256 | 0x80 | 0x80 | 0x9A | 0x74 | 0x80 | 0x08 | 0x80 |
| `gravity` | long | Y speed gained per frame, 16.16 | 0x3800 | 0x3C00 | 0x3800 | 0x3800 | 0x3800 | 0x3800 | 0x3800 |
| `jump` | word | jump strength, x 256 | 0x0680 | 0x0600 | 0x0600 | 0x0580 | 0x0680 | 0x0380 | 0x0680 |
| `jump_release` | word | upward speed kept when the button is let go early, x 256 | 0x0380 | 0x0380 | 0x0600 | 0x0400 | 0x0500 | 0x0200 | 0x0380 |
| `roll_friction` | word | slowing down while rolling, x 256 | 0x0004 | 0x0004 | - | 0x0004 | - | - | 0x0004 |
| `spin_dash_min` | word | spin dash release speed, least, x 256 | 0x0800 | 0x0800 | - | 0x0800 | - | - | 0x0800 |
| `spin_dash_max` | word | most | 0x0C00 | 0x0C00 | - | 0x0C00 | - | - | 0x0C00 |
| `spin_dash_step` | word | added per press while charging | 0x0100 | 0x0100 | - | 0x0100 | - | - | 0x0100 |
| `glide_speed` | word | Knuckles' top gliding speed, x 256 | - | 0x0400 | - | - | - | - | - |
| `glide_acceleration` | word | his gliding speed gained per frame, x 256 | - | 0x0060 | - | - | - | - | - |
| `glide_fall_speed` | word | his falling speed while gliding, x 256 | - | 0x0080 | - | - | - | - | - |
| `air_dash_speed` | word | Vector's air dash speed, x 256 | - | - | - | 0x0600 | - | - | - |
| `wall_cling_time` | word | how long Mighty clings to a wall, frames | 0x0020 | - | - | - | - | - | 0x0020 |
| `wall_jump_speed` | word | jumping off a wall or a climb, 1/4 px per frame | 0x28 | 0x10 | - | 0x18 | - | - | 0x18 |
| `throw_speed` | word | how hard the partner is thrown, 1/4 px per frame | 0x20 | 0x26 | 0x0F | 0x30 | 0x20 | 0x30 | 0x20 |
| `tether_reach` | word | how close the partner must be to grab, px | 0x18 | 0x18 | 0x18 | 0x18 | 0x18 | 0x18 | 0x18 |
| `abilities_a` | byte | bit 0 can jump, bit 2 spin dash, bit 4 Mighty's wall cling, bit 6 glide | 0x1F | 0xCF | 0x08 | 0x0F | 0x01 | 0x01 | 0x0F |
| `abilities_b` | byte | bit 0 Charmy's flight, bit 3 Vector's air dash, bit 6 roll, bit 7 Espio's wall and ceiling cling | 0x40 | 0x40 | 0x03 | 0x4C | 0x30 | 0x00 | 0xC0 |

`-` is `0xFFFF` in the ROM (a field the character does not use). The fields
down to `roll_friction` and `glide_speed`, `glide_fall_speed` and
`air_dash_speed` were verified by changing them and measuring the effect;
the others were read from the code (CHARACTERS.md, section 3, has the
evidence for each). The ability bits are the riskiest: a move a character
never had may need animations or code paths it does not have.

**When it applies.** The stats are written into the ROM in memory before the
game starts, the way a [ROM patch](ROM_PATCHES.md) is (after any patch files
in `Mods/Patches`, with the header checksum fixed). So **a change takes
effect the next time the game is started**, not while it runs. These are
data the game reads, not code, so the recompiled code still runs (the
log's `execution:` line says so). They do change the game: a recorded input
session, or a time in TIME ATTACK, made with different stats will not play
out the same.

Some numbers are not in the table but in code (the walk/run threshold, how
long Charmy's flight burst and Vector's dash last, the spin dash release):
those need a code patch.

## Names

The character names in OPTIONS' TIME ATTACK come from the ROM's own stage
select table (`$8F7254`), so a ROM patch that renames a character shows its
new name there. The unused slot is offered once a patch gives it a name
other than the game's `**********`.

## A new character: combining with sprite replacement

A character is its sprites, its colours and its handling, and each has its
place:

1. **Sprites** in `Mods/Textures`: `32x_<bank>_<frame>.png`, drawn instead of
   the game's frame wherever it is used (TEXTURE_MODS.md). The character's
   body is bank `C` and the extra pieces (Knuckles' gloved hand and ring, for
   instance) bank `C + 8`, with C the character's number (0 Mighty, 2
   Knuckles, 3 Charmy, 4 Vector, 5 Bomb, 6 Heavy, 7 Espio). Draw them with the
   pixel values 1 to 18, the same ones the dump uses.
2. **Colours** in `Mods/Characters/<name>.ini`, `[Palette]`: what those 18
   values look like. The replacement sprites are coloured by the game's
   palette, so with a palette mod they show its colours, and they fade and
   swap between player and partner exactly as the originals do.
3. **Handling** in the same file, `[Stats]`.

For example, Mighty as a blue hedgehog: replace bank 0's frames (and bank 8's)
with the new drawings, using pixel values 2-5 for the body's blues, 10-11 for
the skin and 6-9 for the shoes' and gloves' whites; then

```ini
; Mods/Characters/mighty.ini
[Palette]
2 = #000073   ; body, darkest to lightest
3 = #0000E7
4 = #2152F7
5 = #6B8CFF
6 = #E7E7E7   ; gloves
7 = #A5A5A5
10 = #F7C68C  ; skin
11 = #B57B42
13 = #840000  ; shoes
14 = #420000
15 = #E70000

[Stats]
speed_cap = 0x1000
acceleration = 0x10
deceleration = 0x80
jump = 0x6C0
```

The pixel values a drawing uses are the modder's choice, as long as the
palette gives each one the colour it should have; keeping to the values the
original frames use keeps the parts of the original that are not replaced
looking right.

## Command line

`chaotix_headless --characters DIR` loads `DIR/<name>.ini` the same way, on
the machine and on every machine run beside it (the `--lockstep` reference,
`--compare-native`), so lockstep compares the same modded game. It prints
how many bytes the stats changed and, at the end, how often the shown palette
was worked out and how many blocks it could not match. `--peek-every
N:ADDR:BYTES` prints 68K work RAM every N frames (for measuring a stat:
the player's ground speed in Botanic Base is at `AD74`).

## Validation

- `character_mods.*` unit tests (a synthetic ROM with the game's tables):
  the INI format and its errors; `#RRGGBB` rounding; fades recognised and
  followed, non-fades refused; the shown palette with the mod's colours for
  player and partner, each entry's priority bit kept, the other half's
  copies, a stale offset refused, the same character twice, Espio's cycle
  sets; the stats as bytes of the record, the patch bookkeeping and checksum,
  identity stats changing nothing, stacking on an IPS patch; names from the
  table, slot 1 named by a patch, and the fallbacks.
- `character_mods` (with the ROM, `tests/test_character_mods.py`): every
  character's colours and stats as the ROM has them give pictures
  byte-identical to no mod in Botanic Base 1 (1500 frames, random input,
  4:3 and widescreen), with both of the level's characters recognised,
  lockstep holding and the 68K native; Knuckles recoloured changes only
  pixels of his colours on the 32X layer, each into the colour the mod gives
  it, and nothing on the Mega Drive layer, with lockstep holding; Knuckles'
  acceleration doubled doubles his ground speed's growth, with lockstep
  holding and the 68K native.
- Lockstep with every character recoloured and restatted (acceleration and
  jump), 3000 frames of random input, bit-identical: Mighty and Heavy in
  Botanic Base 1 (4:3 and widescreen), Vector and Charmy in Techno Tower 2
  by day (4:3, and widescreen with the HUD at the edge), Knuckles and
  Espio in Botanic Base 1 (widescreen), Bomb and Espio in the lobby
  (widescreen).
- Cost: Botanic Base 1 in widescreen with random input, about 1.42 ms a
  frame with or without a palette mod; the difference is under 0.01 ms (the
  palette is worked out again only when CRAM or the offsets change). Without
  a palette mod nothing is done.

## Limits

- Only scenes where the characters use the palette blocks above are
  recoloured; levels and the lobby were checked. The bonus and special
  stages were not looked at for this. The PLAYER SELECT screen, the results screen, the title's
  characters and the life icons were not mapped and keep the game's colours
  (the colour check leaves anything it does not recognise alone).
- A fade other than the game's two uniform ones (none was seen) shows the
  game's colours while it lasts.
- Stats need a restart; the abilities bytes can give a character a move it
  has no animation for.
