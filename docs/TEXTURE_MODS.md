# Replacing graphics with image files

Requested in issue #35: graphics mods as image files, without ROM hacking
tools. What the game shows comes from two places, and they need different
approaches (checked with `chaotix_headless --layer md|32x`, which renders one
layer alone):

| | Mega Drive layer | 32X layer |
| --- | --- | --- |
| levels | backgrounds, level art | characters, HUD |
| hub, bonus stage | backgrounds | everything else |
| special stage | the starfield | the tube, the player, the HUD |
| title | the floor | the logo, the characters, the text |

**Both can be replaced**: the Mega Drive layer as 8x8 tiles, the 32X layer
as the sprites the SH-2 draws (characters, the HUD, objects, the title art).
The 32X layer's polygons (the bonus stage's walls, the special stage's tube,
the title intro's) are not graphics and cannot be. How the 32X layer is
drawn, and how replacements are put into it, is in
[SPRITES_32X.md](SPRITES_32X.md). Editing the ROM's graphics with existing
tools and shipping a [ROM patch](ROM_PATCHES.md) also works.

## How to

1. Turn on OPTIONS > EXTRAS > DUMP GRAPHICS (`DumpGraphics = true` in
   `[Mods]`; the older `DumpTiles` is read as the same thing) and play the
   part you want to change. Each graphic the game draws is saved once to
   `Mods/Textures/Dump`, as a palette-indexed PNG showing it as it was
   drawn. Turn it off again when done.
2. Copy the files you want to change into `Mods/Textures`, keeping their
   names, and edit them in an editor that keeps the image indexed (Aseprite,
   GIMP in Indexed mode, ...).
3. Start the game: each file in `Mods/Textures` is drawn instead of the
   graphic it is named after, wherever and whenever the game uses it.

Only the picture changes. The game's machine -- its frame buffers, the video
chips' registers and status flags, the timing -- runs exactly as without the
mod (the recompiled code stays bit-identical to the interpreter with a pack
loaded), so a mod cannot change how the game plays.

Full-colour PNGs are refused, with a message in the log: the colours must be
indices into the game's palette, so that the palette's fades and cycling,
and the colour offsets below, still apply. Changing which colours a palette
holds is a ROM patch's job.

## Mega Drive tiles: `md_<hash>.png`

- An 8x8 tile, named by a hash of its pixels, not its place in video memory,
  so it is replaced wherever the game uploads it. Keep it 8x8.
- The PNG's colours are palette indices: 1-15 are the 15 colours of the
  tile's palette line, 0 is transparent. The same tile drawn with another
  palette line uses the same replacement, in that line's colours.
- The game's view of the VDP (the sprite collision and overflow flags in
  particular) still comes from the original tiles.

## 32X sprites: `32x_<bank>_<frame>.png` and `32x_<hash>.png`

- **Names.** A graphic in the game's bank table is named by its bank and
  frame, two hex digits each, lower case: `32x_02_51.png` is bank 2 (Knuckles)
  frame 0x51. That name is stable: it is the same graphic in every level,
  as the player or the partner, wherever the game keeps it. The few graphics
  not in the table (title and menu pieces, the lobby's) are named by a hash
  of their bytes, `32x_<16 hex digits>.png`. A graphic with a bank and frame
  is looked up by them first, then by its hash; so a hash name also works for
  a graphic of a patched ROM.
- **Colours.** The pixel values are the graphic's own, before the game
  adds anything: the characters are drawn with a colour offset added to each
  pixel (0x12 for the player's character, 0x24 for the partner), and a draw
  can take the other half of the palette (flag bit 2). Both still apply to the
  replacement, as do the fades and each palette entry's priority bit (which
  decides, per colour, whether the 32X shows in front of the Mega Drive
  layer). So keep to the values the dump uses for each part of a character;
  the dump's palette shows them in the colours of the draw it was taken from
  (the player's or the partner's, whichever came first). 0 is transparent.
  Values from 0x80 up are drawn as the game's blitters draw them, which
  smears them into neighbouring pixels in some cases; the game's own
  graphics stay below 0x80.
- **Size and place.** The dump is the graphic's box: `x1 - x0` pixels wide
  and `(y1 - y0) / 256 + 1` lines high (the box's bottom line is inclusive),
  its top-left corner where the game puts the graphic's corner relative to
  the draw's position. A replacement of the same size is drawn exactly over
  the original. A replacement of another size gets a box of its own size at
  the same top-left corner, and so grows to the right and down (to the left
  and up when the game flips the draw). It must stay within 127 pixels of
  the draw's position (the game's graphic format stores columns and lines as
  bytes); a larger one is not drawn (the original is, and the log says why).
  Being drawn at the game's resolution, a replacement can be more detailed
  only within that resolution: high-resolution replacements would need the
  compositor to output more than 320 pixels, which is not done.
- **Drawing.** A replacement is drawn the way the game draws the original:
  flipped, scaled (and stretched, for the waving SEGA logo), clipped, and
  with the every-second-column transparency the game uses for flashing, all
  at the same positions with the same rounding. Things the game draws later
  still cover it (a polygon, another sprite), and it covers what was drawn
  before it, in the game's order; where the replacement is transparent and
  the original was not, what lay under the original shows.
- **Several pieces.** Some objects are several graphics: Espio is two or
  three, the title's CHAOTIX logo four, and the HUD one per label and digit.
  Each is its own file.
- **Widescreen.** Replacements are drawn into the widescreen margins too,
  as the originals are.

Known limits:

- A replacement larger than the original that reaches over a polygon drawn
  *before* the sprite covers it (the game's order); one that reaches past
  the clip rectangle is clipped there, like the original.
- In a widened level, any fill that starts at a line's first column clears
  that line's margins (`Machine::fb_margin_clear_line`, see SPRITES_32X.md).
  In the levels checked, the only such fills drawn after sprites are the
  bars of the title card as a level starts; on those frames, sprite pixels
  in the margins of those lines are cleared, original or replaced.
- The first picture after a pack is loaded mid-game, or after a return to
  the title, is shown as the game drew it: a picture is composed only when
  it was followed from its start.

## Validation

- `tile_replace.*` unit tests: sprite and plane tiles replaced, transparency,
  the collision flag following the original tiles both ways, dumping once with
  the palette.
- `sprite_replace.*` unit tests: a dumped graphic as its own replacement draws
  exactly the original with every flag, odd positions, the colour offset,
  both scaled blitters; replaced pixels flipped, scaled, stippled, coloured
  and taken away; another size at the same corner; too large refused; the
  composed picture over fills and polygons drawn before and after, its
  output, the widescreen margins; nothing composed without a replacement or
  before a buffer is followed from its start; dumping once with the drawn
  colours; names, loading, malformed and full-colour files refused.
- `image_io.*` unit tests: the PNG reader (all colour types, bit depths,
  filters, fixed and dynamic Huffman, damaged and unsupported files) and the
  indexed writer.
- `tile_pack` (with the ROM): Botanic Base's dumped tiles loaded back as a pack
  give exactly the original pictures, and the recompiled machine stays
  bit-identical to the interpreter (lockstep, random input).
- `sprite_pack` (with the ROM): the graphics dumped in Botanic Base 1, the
  title, the lobby, the bonus stage and the special stage, in widescreen and
  4:3, loaded back as a pack give exactly the original picture in every
  frame (1500-1900 frames each, random input), with lockstep holding with
  the pack on both machines. A pack with every sprite pixel changed changes
  the 32X layer in most frames, leaves the Mega Drive layer as it was, leaves
  every byte that polygons and fills drew last as it was, and keeps lockstep.
- The cost, Botanic Base 1 in widescreen with random input (about 1.45 ms a
  frame without mods): a sprite pack loaded but none of its graphics in the
  scene, about 0.08 ms a frame (recording the draws and following the frame
  buffer writes); replacing the scene's sprites, about 0.17 ms. A tile pack,
  about 0.04 ms. Nothing without a pack, or with the dump off.
