# The characters as data

Research for character mods (a modder turning Mighty into Sonic and Charmy
into Tails, with new looks and handling): where each character's sprites,
colours, movement parameters and names live, so that palette overrides,
whole-character sprite sheets and a stats file can be built on them. No
feature code came with this research.

Addresses: `$88xxxx` is 68K code/data in ROM at offset `xxxx + 0x000000`
(window 0x880000 = ROM 0). `$9xxxxx` is the banked window; in levels the
bank register (A15104) is 2, so `$92A16A` is ROM `0x22A16A`. SH-2 addresses
`0x02xxxxxx` are ROM offset `xxxxxx`. RAM `FFxxxx` is 68K work RAM.
Character ids are as in `FFE038`/`FFE03A` divided by 4: 0 Mighty, 1 unused,
2 Knuckles, 3 Charmy, 4 Vector, 5 Bomb, 6 Heavy, 7 Espio. Tables indexed by
`FFE038` have eight 4-byte entries in that order.

How it was found: a probe linked against the build's libraries (load the ROM,
`--stage` as in `chaotix_headless`, scripted or `--fuzz` input, dumps of work
RAM and of `mars.pal`, a watch of 68K data reads in a ROM range and of 68K
writes to the 32X palette, and in-memory ROM edits with the header checksum
fixed), plus disassembly. "Verified" below means a ROM value was changed and
the effect measured in RAM over frames.

## 1. Sprites

### Banks

Each character has two banks in the 32X bank table (ROM `0x0A0000`, 128
pointers): bank `C` (the body) and bank `C+8` (the tether hand and ring, and
extra pieces). A frame table is a list of graphic pointers; `FFFFFFFF` is an
empty slot. Counts are slots up to the first graphic, from the ROM:

| char | bank | frame table (ROM) | slots | empty slots | bank+8 | table | slots | empty |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Mighty | 0 | 0A0504 | 92 | 08, 47-4B | 8 | 11B088 | 84 | 3B-3E |
| (slot 1) | 1 | 0B18B8 | 1 (`FFFFFFFF`, then runs into bank 2's table) | | 9 | 11F7A4 | 0 (first word 0) | |
| Knuckles | 2 | 0B18BC | 113 | - | 10 | 11F7A8 | 36 | - |
| Charmy | 3 | 0CB3B4 | 125 | 49 | 11 | 121AA4 | 75 | - |
| Vector | 4 | 0D4E44 | 111 | - | 12 | 1256B0 | 60 | 38-3A |
| Bomb | 5 | 0F1130 | 46 | 1F-23 | 13 | 1293E0 | 22 | - |
| Heavy | 6 | 0F6A18 | 45 | 01 | 14 | 12A9E8 | 16 | - |
| Espio | 7 | 101304 | 106 | - | 15 | 12C120 | 60 | - |

Bank 10 (Knuckles' second set, rendered from the ROM): frames 00-03 the
spinning ring, 04-23 a gloved hand holding the ring at 32 angles. Bank 2:
00-03 idle, 04-0B walk, 0C-23 walk on slopes, 24-33 run, 34-39 spin dash
ball, 3A-3F skid/hurt/etc., 40-44 roll, 45-57 tether poses, 58-5F glide,
60-6F climb, 70 a graphic in other colours (probably a menu/lobby picture).
Heavy's bank 6 likewise ends with an odd one (2C). The other banks were not
labelled frame by frame; `sheet.py`-style rendering (decode the graphic
format of SPRITES_32X.md, colours `pixel + 0x12` from section 2) is how.

**Recorder gap (bug).** `SpriteRecorder::rebuild_index`
(`src/runtime/sprite_record.cpp`) stops a frame table at the first entry that
is not a graphic's address, so an `FFFFFFFF` hole ends it: Mighty frames 09+,
Charmy 4A+, Bomb 24+, Heavy 02+ (all but frame 0), Mighty-2 3F+, Vector-2 3B+
are not named by bank and frame, only by hash (seen: Heavy as player draws
only `bank 6 frame 0` in `--sprites-log`). Skipping `FFFFFFFF` (continue,
not break) would fix it; a sprite-sheet feature needs that first.

### Who chooses the frame

68K, per frame (in bank 2 code, so only while the bank register is 2):

- `$928EEC`: the animation step. The character comes from `FFE038`
  (object `+6` = 0) or `FFE03A` (partner). Two tables indexed by it:
  `$928F56` animation-script tables, `$928F76` frame-record tables.
- The object's animation id is the word at `+0x26` (even), its frame
  counter `+0x28` (8.8). Script = `anim_table + word[anim_table + id]`:
  `speed, last, loop, frame records...`. Each step adds `speed` to `+0x28`
  (speed 0: the movement code adds a speed-dependent amount itself, walk
  and run); past `last` it goes to `loop`.
- Frame record = `frame_table + word[frame_table + 2*index]`: `rx, ry`
  (collision half-size, written to object `+0x22/+0x23`, divided by the
  object's scale), then pieces of 4 bytes `dx, dy, frame, flags` until a
  piece with flag bit 7. The object keeps a pointer to the pieces at `+0x10`.
- `$8811EC`/`$88128C` turn the pieces into 32X sprite commands: bank = the
  object's byte `+0x20`, frame from the piece, flags (h/v flip XOR the
  object's `+0x21`, bit 2 palette half) low bits. So the bank comes from the
  object, the frame from the piece: the body object has bank C, the
  hand/ring sub-object bank C+8.

Tables (68K address; ROM = address - 0x900000 + 0x200000):

| char | animations | frame records | records |
| --- | --- | --- | --- |
| Mighty (and slot 1) | $929D18 | $929E2A | 104 |
| Knuckles | $92A16A | $92A2B2 | 124 |
| Charmy | $92A692 | $92A82A | 135 |
| Vector | $92AC62 | $92ADB2 | 117 |
| Bomb | $92B15A | $92B270 | 50 |
| Heavy | $92B3F4 | $92B4E6 | 51 |
| Espio | $92B67E | $92B84E | 119 |

All records seen have a single piece, and for most the piece frame equals
the record index; records at the end (Knuckles 70-7B, Mighty 5B-67, Espio
69-76) point at frames 00-0C again with a large `dx` (0x14-0x1C) and are
used by animations 3C/3E, which look like the ring/hand sub-object's
(*not verified*).

### Animation ids (shared numbering, from the code that sets `+0x26`)

| id | use | evidence |
| --- | --- | --- |
| 00 | stand | default; frames 00-03 |
| 02, 04, 06, 08 | walk, by slope sector | `$88A03C`: `2 + angle bits` while \|speed\| < 0x400 |
| 0A, 0C, 0E, 10 | run, by slope sector | same, base 0x0A when \|speed\| >= 0x400 |
| 12 | roll / jump ball | set on jump and roll |
| 16, 18 | balancing on a ledge | `$889EF2`, `$889F12` |
| 1A | skid | `$88A0DC` |
| 1C-2C | repeat 00-10 (variant not identified) | same scripts |
| 2E | spin dash charge | `$88A8DA` |
| 30 | hurt | state 0x12, `$88A98A` |
| 36 | tether pull/throw pose | `$88A77E` |
| 40+ | character's own: Knuckles 40/42 glide, 44 climb; Vector 40-44 dash; Espio 44 wall/ceiling cling... | per-character scripts, partly confirmed (glide = state 0x24 with id 0x40) |
| 14, 32, 34, 38, 3A, 3C, 3E, 46, 48 | not identified | |

Character select, lobby and title art: in the lobby the characters are the
same banks (Espio bank 7 frames 08/0B with bank 15 frame 29; Knuckles bank 2
frame 0x52 with bank 10 frame 3 on the crane), with the same colour offsets.
The title's characters are bank-table graphics of their own (not banks 0-15,
see SPRITES_32X.md); not mapped here.

## 2. Palettes

### In levels and the lobby (verified)

Characters are drawn only on the 32X. A character pixel `p` (1-18) is shown
in 32X palette entry `offset + p`, offset from the SH-2 byte
`0x06004B00 + bank` (0x12 player, 0x24 partner), so:

- player: entries **0x13-0x24**, partner: **0x25-0x36** (18 colours each);
- entries **0x93-0xA4 / 0xA5-0xB6** hold the same colours with the priority
  bit (bit 15) inverted, for draws with flag bit 2 (the other palette half);
- entries 0x01-0x12 are a common set loaded with them (ROM `0x001924`).

Loader `$8818AE`: `$8830B8(1, 18 colours from $881924)`, then
`$8830B8(0x13, [$8818E4 + FFE038])` and `$8830B8(0x25, [$8818E4 + FFE03A])`.
`$8830B8` writes `d1+1` words straight into CRAM (A15200) at entry `d0`,
each also at `d0 + 0x80` with bit 15 flipped. Pointers at `$8818E4` (8 longs,
banked; ROM = ptr - 0x700000):

| char | 68K | ROM | 
| --- | --- | --- |
| Mighty, slot 1 | $92921A | 0x22921A |
| Knuckles | $92923E | 0x22923E |
| Charmy | $92927E | 0x22927E |
| Vector | $92925C | 0x22925C |
| Bomb | $9292A0 | 0x2292A0 |
| Heavy | $9292BE | 0x2292BE |
| Espio | $9292DC | 0x2292DC |

The blocks overlap (each is read as 18 words; e.g. Knuckles' last three are
Vector's first three), so a ROM patch of one character's colours can change
another's: a host-side override is the safe route. Checked: CRAM 0x13-0x36
equals these words exactly in Botanic Base 1 (Knuckles + Espio), and is the
same at MORNING, SUNSET and NIGHT (time of day does not touch them).

Callers of `$8818AE`: `$881986` and `$8A5974, $8AA004, $8AD5C6, $8AD6EA,
$8ADC0E, $8AE0BA, $8AE466, $8BE1D4` (scene set-ups: level start, lobby;
the individual scenes were not mapped). A character swap (the combine at
`$890220`, which exchanges `FFE038`/`FFE03A`) goes on through `$88F814`.

**Other writers of these entries:**

- Fades, in place in CRAM: fade-out `$882F78` (each component -1 per step);
  fade-in `$882FCE` steps each component of CRAM by 1 towards a target
  palette in RAM at `FFD660`. A full black clear at `$883936`.
- Whole-palette loads at `$883102` (no priority flip) and in bank 2 at
  `$9297D8` (level palette).
- **Espio's colour cycling** (`$88E70A`): 16 sets of 5 colours at
  `$88E7BA` (ROM `0x0E7BA`, 160 bytes), written through the vblank palette
  queue (`FFD860`, flushed by `$8831AE`) to Espio's pixels 2-6, i.e. entries
  0x14-0x18 as player, 0x26-0x2A as partner. In the lobby the partner's
  0x26-0x2A held another set (`0048 0090 10F8 359D 3E7E`): that is set 11
  of the 16, and in the lobby with random input every set came round in
  turn. Set 0 is Espio's own pixels 2-6 as `$8818E4` loads them. An Espio
  replacement must override these sets too (character mods' `[Cycle]`).
- The queue also cycles 0x37-0x3A (`$891784`), 0x42-0x47 (`$8B1648`) and
  0x58-0x5E: not character colours.

Seen with the character mods' gate (which reports blocks it could not match,
`chaotix_headless --characters`):

- **Fades are uniform.** Through a whole fade-out (in the lobby, after the
  finished-run recording's level, steps 1 to 31) every component of the 18 entries was
  `max(original - n, 0)` with one `n` for the block, as `$882F78` implies.
  No fade-in of character colours was seen: at a level start they are loaded
  after the screen is already up.
- **Stale offsets.** On the title screen and its menus bank 0's offset byte is
  0x24 and entries 0x26-0x36 hold Mighty's colours, but 0x25 holds `1C00`
  (the 32X layer there uses it for something else). The offset bytes keep the
  last level's values through other scenes.
- **The same character twice.** After a run the lobby can have the same
  character as player and partner (Bomb and Bomb in the finished-run
  recording): both blocks hold its colours, but its bank has one offset byte,
  0x24, so the byte names only the partner's block.

### Where to substitute host-side

`Machine::render_line32x` (`src/renderer/compositor.cpp`) reads
`mars.pal` per line (`encode_idx(idx, mars.pal, pri)`). Passing a substituted
copy instead changes only the picture: CRAM, the CPUs and the frame buffers
are untouched, so `--lockstep` compares the same machine state (the
reference renders with the same override). Nothing in the game reads the
displayed colours back (the fade reads CRAM, which stays emulated).

Mega Drive side: in levels and the lobby the characters do not appear on the
Mega Drive layer (TEXTURE_MODS.md's layer table; checked with the CRAM dumps
above). Results screen, the PLAYER SELECT screen and the life icons were
**not** checked.

## 3. Movement parameters

### The table (verified for the fields marked V)

`$889D8E`: `a4 = [$88BE58 + FFE038 or FFE03A]`, one 0x4A-byte record per
character, read by the shared player code at `$889DF2`-`$88BE58` (state
machine on object byte `+7`, jump table `$889DBC`). Also read at `$8B003A`
(field 0x12, partner proximity).

| char | record (68K = ROM + 0x880000) |
| --- | --- |
| Mighty, slot 1 | $88BE78 |
| Knuckles | $88BEC2 |
| Charmy | $88BF0C |
| Vector | $88BF56 |
| Bomb | $88BFA0 |
| Heavy | $88BFEA |
| Espio | $88C034 |

Object fields used below: `+8` X (16.16), `+C` Y, `+18` X velocity, `+1C`
Y velocity (16.16 px/frame), `+2C` ground speed (8.8 px/frame), `+26`
animation, `+7` state, `+3A` pointer to the character's status object
(its `+34` speed-shoes timer doubles acceleration; its `+38` doubles jump,
tether reach and dash strength, and with its `+30` bit 3 they become 0.5x /
0.75x: a power-up, not identified). Player object `FFAD48` (`FFE034`),
partner `FFAE08` (`FFE036`) in Botanic Base 1.

| off | size | meaning | Mig | Knu | Cha | Vec | Bom | Hea | Esp | conf. |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 00 | b | character bit (1 << id) | 01 | 04 | 08 | 10 | 20 | 40 | 80 | high (not read in play) |
| 01 | b | abilities A: bit0 can jump, bit2 spin dash, bit4 wall cling (Mighty), bit5 state 0x22 (nobody), bit6 glide | 1F | CF | 08 | 0F | 01 | 01 | 0F | high for 0,2,4,6 |
| 02 | b | abilities B: bit0 flight jump (Charmy), bit3 air dash (Vector), bit4 Bomb's state 0x30 after landing, bit6 roll, bit7 wall/ceiling cling (Espio) | 40 | 40 | 03 | 4C | 30 | 00 | C0 | high for 0,3,6,7; low for 4 |
| 04 | w | velocity cap per axis, px/f x 256 (`$88B72E`); ground speed itself has no cap | 1000 | 0E00 | 0C00 | 0C00 | 0800 | 0600 | 1000 | V |
| 06 | w | ground acceleration, also friction with no input (8.8/frame) | 0C | 0A | 0E | 07 | 0C | 04 | 0C | V |
| 08 | w | deceleration when pressing against motion (skid) | 80 | 80 | 9A | 74 | 80 | 08 | 80 | high |
| 0A | l | gravity (16.16 per frame) | 3800 | 3C00 | 3800 | 3800 | 3800 | 3800 | 3800 | V |
| 0E | w | not read in any run | 0100 | 0120 | 0038 | 0150 | 0100 | 0180 | 0100 | - |
| 10 | w | throw-partner speed (x sin, 1/4 px/f units), `$88BD1E` | 20 | 26 | 0F | 30 | 20 | 30 | 20 | medium |
| 12 | w | tether grab reach, px (both axes) | 18 | 18 | 18 | 18 | 18 | 18 | 18 | high |
| 14 | w | jump-release cap: Y velocity limited to -this x 256 when the button is let go | 0380 | 0380 | 0600 | 0400 | 0500 | 0200 | 0380 | V |
| 16 | w | jump strength (8.8, along the surface normal) | 0680 | 0600 | 0600 | 0580 | 0680 | 0380 | 0680 | V |
| 18 | w | spin dash minimum speed | 0800 | 0800 | - | 0800 | - | - | 0800 | high |
| 1A | w | spin dash maximum | 0C00 | 0C00 | - | 0C00 | - | - | 0C00 | high |
| 1C | w | spin dash added per press | 0100 | 0100 | - | 0100 | - | - | 0100 | high |
| 1E | w | wall cling time, frames (Mighty) | 0020 | - | - | - | - | - | 0020 | medium |
| 20 | w | wall/climb jump-off speed (1/4 px/f) | 28 | 10 | - | 18 | - | - | 18 | medium |
| 22 | w | timer for state 0x22 (unused) | - | - | - | - | - | - | - | - |
| 2A | w | glide max X speed (x 256) | - | 0400 | - | - | - | - | - | V |
| 2C | w | glide X acceleration (x 256) | - | 0060 | - | - | - | - | - | high |
| 2E | w | glide fall speed (x 256) | - | 0080 | - | - | - | - | - | V (Y vel 0x8000) |
| 30 | w | Charmy flight (state 0x2C) parameter, probably burst count | - | - | 0003 | - | - | - | - | low |
| 32, 34 | w | Bomb only, not read in any run | - | - | - | - | 0400, 0100 | - | - | - |
| 36 | w | rolling friction | 0004 | 0004 | - | 0004 | - | - | 0004 | high |
| 38 | w | air dash speed (Vector) | - | - | - | 0600 | - | - | - | V |
| 3A-49 | 8 x (dx,dy) | where a held partner sits, per direction (`$88B806`) | same for all | | | | | | | medium |

`-` is `FFFF` in the ROM.

Measurements (Knuckles, Botanic Base 1, holding right from frame 830 /
pressing C to jump at 850):

- 06: 0x0A -> 0x14 doubles the ground-speed ramp (`+2C` at frame 852: 0xE6
  -> 0x1B8; at 984: 0x60E -> 0xBB3).
- 04: 0x0E00 -> 0x0300 holds X to exactly 3 px/frame (0x4B0000 per 25
  frames) while `+2C` keeps rising.
- 16: Y velocity just after the jump -0x5xxxx -> -0x8xxxx (0x600 -> 0x900),
  apex 73 px -> 96 px (the higher one hit a ceiling).
- 0A: Y velocity steps 0x3C00 per frame; 0x1E00 after the patch.
- 14: a 3-frame tap leaves Y velocity at exactly -0x38000; -0x10000 with
  0x0100.
- 2A/2E: glide (state 0x24) X velocity settles at 0x40000; 0x20000 with
  0x0200; Y velocity 0x8000.
- 38 (Vector): air dash (state 0x2E, 8 frames) at 0x60000; 0x30000 with
  0x0300.

**Character-specific code.** Two more tables indexed by `FFE038`: `$889CEE`
(Mighty `$88C07E`, Knuckles `$88C6A4`, Charmy `$88CC9E`, Vector `$88D25C`,
Bomb `$88D642`, Heavy `$88D8E4`, Espio `$88DCBE`) and `$889D66` (Charmy
`$88E1C0`, Vector `$88E34E`, Bomb `$88E4E2`, Espio `$88E5B4`, others an
`rts`). These were not read; constants in them (and immediates in the shared
code, such as the 0x400 walk/run threshold at `$88A022`, the 0x500 minimum
in `$88B6D0`, Charmy's 0x10-frame flight burst at `$88B922`, Vector's 8-frame
dash at `$88B9F2`, spin dash release at `$88A940`) are code, and changing them means patching code (the
68K then runs in the interpreter, see ROM_PATCHES.md).

Player states seen (`+7`): 02 ground, 04 jump, 06 roll, 08 airborne, 0A/0C
holding the partner (ground/air), 0E held, 10 spin dash charge, 12 hurt,
18 launched by a surface, 1A skid, 20 Mighty wall cling, 22 unused, 24
Knuckles glide, 26/28 wall climb variants, 2A/2C Charmy flight, 2E Vector
air dash, 30 Bomb, 32 Espio cling. 1C/1E are holes (`4E75` in the table).

## 4. Names

- **Stage select** (`stage_select.h`): `$8F7254` (ROM **`0x077254`**: the
  window `$880000-$8FFFFF` is ROM `0x000000-0x07FFFF`), 8 signed word
  offsets indexed by character id, each counted from the word itself, to
  NUL-terminated strings padded to 10 characters with spaces at `$8F74E8`
  (ROM `0x0774E8`): `MIGHTY`, `**********`, `KNUCKLES`, `CHARMY BEE`,
  `VECTOR`, `BOMB`, `HEAVY`, `ESPIO`, then `1 PLAYER`, `2 PLAYERS`. Printed by
  `$8F7244` (via `$8F7562`). The front end's TIME ATTACK reads its names from
  here (`stage_select::character_names`), so a ROM patch that renames a
  character, or names slot 1, shows there.
- **PLAYER SELECT / COMBI SELECT screen**: `$8B2678`, 32-byte entries
  (a signed word x adjustment, e.g. `FFB0`, then 30 characters) in **select
  order**, not id order: KNUCKLES THE ECHIDNA, MIGHTY THE ARMADILLO, ESPIO
  THE CHAMELEON, CHARMY BEE, VECTOR THE CROCODILE, BOMB THE MECHANIC, HEAVY
  THE MECHANIC; indexed by `FFE20E`, drawn by `$8B25E6`/`$8B262C` through a
  glyph map at `$8B2658`. Titles `PLAYER  SELECT` at `$8B2758`, `COMBI
  SELECT` at `$8B2776` (chosen by `FFDFE0`). The mapping from select order to
  character id was not traced.

## What this means for mods (proposal)

The palette override and the stats file are now built: see
[CHARACTER_MODS.md](CHARACTER_MODS.md). They differ from the proposal below
in one point: a fade is followed by applying the same fade (the same `n`
steps, out or in) to the mod's colours rather than by scaling them, which is
what the game would show if the mod's colours were in the ROM, and needs no
special case for a component that is 0.

- **Palette override** (output only): `Mods/Characters/<name>.ini` or
  `Mods/Palettes/<char>.pal` with 18 BGR555 (or RGB) colours for pixels 1-18,
  plus optionally Espio's 16x5 cycle sets. At composition, build a display
  copy of `mars.pal`: for each character on screen (bank `C` and `C+8`
  whose offset byte at SDRAM `0x06004B00 + C` is 0x12 or 0x24), entries
  `offset+1..offset+18` and `+0x80` copies take the mod colours, keeping each
  entry's priority bit from CRAM. Gate it by comparing CRAM with the ROM's
  colours for that character (`$8818E4`), and follow fades by scaling each
  component of the mod colour by `cram/original` (exact when equal, black at
  black). Hook: `render_line32x`, which already reads the palette per line.
  Lockstep-safe: nothing emulated changes.
- **Whole-character sprite sheets**: export banks `C` and `C+8` from the ROM
  (frame tables above, empty slots skipped) as one indexed PNG per bank
  with a JSON/INI of cells (frame, box `x0 x1 y0 y1`, hotspot); import by
  splitting the sheet into the existing `32x_<bank>_<frame>.png`
  replacements (fix the recorder's `FFFFFFFF` stop first).
- **Stats file**: `Mods/Characters/<name>.ini` naming the character slot
  and the fields of section 3 by name (`accel = 0x0C`, `jump = 0x680`...).
  Applied as an in-memory data patch of the 0x4A-byte record (ROM
  `0x0BE78 + 0x4A * k`), checksum fixed, before `reset()`, on **both**
  machines exactly like `--patch` (the reference gets the same change), so
  lockstep still compares like with like. These are data reads, so the 68K
  stays native. Values in code (section 3) need a code patch.

## Not determined

- Which scene each `$8818AE` caller is; the PLAYER SELECT screen's and the
  results screen's palettes and whether any character art is on the Mega
  Drive layer there; the title's character graphics.
- The meaning of stat field 0E, Bomb's 32/34, Charmy's 30 (low confidence),
  the power-up behind status `+38`, byte 01 bits 1/3/7.
- Animation ids 14, 32, 34, 38-3E, and per-frame labels for every bank.
- The character-specific routines at `$889CEE`/`$889D66`.
