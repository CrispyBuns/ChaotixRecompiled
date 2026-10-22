# The menu's font and backdrops (issue #8)

Issue #8 asked for the menu to look like the game: its own font instead of
the bundled pixel face, and pictures of its zones behind the menu instead of
plain colour. Both are made from the player's ROM when the program runs;
nothing of the game is in the repository or in a release.

| Setting | Values | Default | `[Video]` key |
| --- | --- | --- | --- |
| OPTIONS > EXTRAS > MENU FONT | GAME, PIXEL | GAME | `MenuFont = Game` |
| OPTIONS > EXTRAS > MENU BACKGROUND | ZONE, PLAIN | ZONE | `MenuBackground = Zone` |

PIXEL and PLAIN are the look from before. The new look is the default
because it is what was asked for, both parts come from the ROM the player
already has, and both fall back on their own: a ROM without the font at its
address keeps the pixel face, and until a zone's picture exists the menu is
plain. The two rows are under EXTRAS because VIDEO already has as many rows as
a phone's screen holds.

## The font

The TRAINING and DATA LOAD screens write zone names ("ISOLATED ISLAND") with
an 8x16 font of Mega Drive sprites: white digits shaded in lilac, yellow
capitals shaded in brown, a black drop shadow on every glyph. It was
found by dumping VRAM on the TRAINING screen, reading the VDP's sprite table
(each letter is a 1x2-tile sprite: `I` is tile 0x524, `S` 0x538, ...), and
logging the 68K writes into those tiles: the decompressor at $8F49C6 writes
them straight to the VDP data port from $9C4B20 (ROM bank 2).

- **Tiles:** ROM 0x2C4B20, Nemesis-compressed (the usual Sega format), header
  0x805E: XOR mode, 94 tiles. Decoded, they are exactly what the game puts in
  VRAM at 0xA000.
- **Glyphs:** 47 of them, two tiles each (top, then bottom), in this order:
  `0`-`9`, `A`-`Z`, `?` `!` `-` `,` `.` `♂` `♀` `_` `“` `”` `’`. There is no
  lower case and no space glyph (the game leaves a gap). Every glyph is 8
  pixels wide, ink in columns 1-7 (the shadow included) and lines 1-15.
- **Colours:** palette line 0 of those screens, 16 words at ROM 0x03E8CA.
  The digits use entries 1-3 (white, lilac, dark purple), the letters 10-12
  (yellow, brown, dark red), the shadow entry 5 (black).

`renderer/game_font.*` decodes it (`nemesis_decode`, `build_game_font`) and
builds an atlas of 50 glyphs, 400x16: the ROM's 47, plus `:` (the full
stop's dot twice) and `<` `>` (triangles in the letters' yellow with the
shadow), which the menu's values and arrows need. Lower case is drawn as
upper case, `"` and `'` as the curly ones. The header and the palette words
are checked, so a different ROM is refused rather than misread.

`ui::Ui` draws the pixel fonts (the menu's labels, values and headings) with
it when MENU FONT is GAME and the font has every character of the string; a
string with anything else (`%`, `/`) keeps the pixel face, whole, centred in
the same line. Glyph pixels become whole screen pixels: the scale is the
pixel face's size over 16, rounded (1 at 720p for labels and 2 for headings,
2 and 3 at 1080p), positions are rounded, and the texture is sampled
nearest-neighbour. The text's colour only sets the glyphs' brightness (full
for the selected entry and headings, about two thirds for the others), so
the game's own colours are kept. Captions and hints keep the small text
face; the touch controls and notifications keep the pixel face.

`chaotix_headless --game-font FILE.png` writes the atlas.

## The backdrops

A backdrop is a picture of a zone at a time of day, made by the game itself:
`render_backdrop` (`frontend/backdrop.*`) builds a second `Machine` from a
copy of the ROM, starts the stage the way TIME ATTACK does (the request at
the mode dispatcher, Start tapped until it is taken), runs 480 frames more so
the title card has gone, and keeps the frame buffer. The playing machine is
never involved.

- **Wide:** rendered with 40 extra columns a side (400x224, about 16:9), as
  WIDESCREEN renders levels. The lobby is 4:3, as the game keeps it.
- **No characters or HUD:** the backdrop machine's sprite recorder gets a pack
  that replaces every frame of banks 0-15 (the characters) and 0x13 (the
  HUD) with an empty image, so they are left out of the picture
  (`runtime/sprite_replace.h`; output only, like every replacement).
- **Interpreted:** the recompiled code keeps one machine's state in globals
  (`game/recomp_dispatch.cpp`), and that machine is the one being played, so
  the backdrop machine runs on the reference interpreters, which keep all
  their state in the machine. That is what lets it run on another thread.

The zones, in the order they are made and shown: Marina Madness 2 at sunset
(the picture the issue showed), Botanic Base 3 in the morning, Speed Slider 1
at night, Techno Tower 1 at sunset, Amazing Arena 2 by day, and Isolated
Island (the lobby) at sunset. Botanic Base 1 is not used: it starts by the
level's left edge, which shows tiles from beyond it.

**Cost.** About 620 emulated frames each, interpreted: 1.8-2.3 s per picture
on the development PC (Windows, about 330 frames a second interpreted), 11-13
s for all six. A phone is maybe 3-10 times slower, so tens of seconds per
picture there. It happens once per ROM, on a low-priority thread
(`platform/backdrops.*`), one picture at a time, and the menu does not wait:
it is plain until the first picture is ready and picks the others up as they
arrive. With MENU BACKGROUND on PLAIN nothing is made at all.

The worker never competes with the game: slow machines have no core to spare
(issue #31's Celeron needed 20-37 ms a frame; phones share caches and thermal
headroom between few cores). It starts only while the front end or a menu
page is open, and the moment the game is played with no menu open it is held
between two frames of its machine (`BackdropControl::pause`, polled before
each one; a frame takes about 3 ms on the development PC) -- paused, not
cancelled -- and it carries on from there when a menu opens again. So on a
slow machine the pictures appear over a few visits to the menu. Autotests
still wait for the first picture before they run.

**Cache.** Each picture is written as a PNG to
`<user data>/Cache/Backdrops/v1-<first 12 hex digits of the ROM's SHA-1>/<zone>.png`
(the patched image's SHA-1 when ROM patches are applied, since they can change
the levels' art), and later starts load them at once. Delete the folder to
make them again. They are derived from your ROM and stay on your device.

**On screen.** Each time the menu opens it shows the next zone. The picture
covers the area (cropped to its shape, never stretched) and a translucent
coat of the zone's own colour goes over it (`backdrop_tint`: the picture's
average colour at a third of its brightness) so that the white and yellow
letters stay readable: over the whole window behind the front end and the
awards, inside the panel of the list pages, inside the band of the pause row.
The frames, markers and arrows take the zone's most common vivid colour
(`backdrop_accent`) instead of the theme's blue. In a level the game stays
visible around the panel, dimmed as before.

`chaotix_headless --backdrop all|KEY|PLACE:LEVEL:TIME[:WIDE] DIR` renders
backdrops to `DIR` and says how long each took.

**Lockstep.** Nothing here writes to the playing machine. The ROM test
`game_font.a_backdrop_renders_beside_a_playing_machine` plays 700 frames on
the recompiled machine while a backdrop is made on another thread, and the
frames must hash the same as a run without it; `lockstep_*` and the golden
frame tests are unchanged.
