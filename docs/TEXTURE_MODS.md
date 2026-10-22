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
holds is a ROM patch's job, or, for the characters' colours, a
[character mod](CHARACTER_MODS.md)'s, which also sets how they move.

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

## Whole banks as sheets: `32x_<bank>.png` with `32x_<bank>.txt`

A character is over a hundred frames in two banks (its body in bank C, the
tether hand, ring and extra pieces in bank C+8; [CHARACTERS.md](CHARACTERS.md)
has the banks), and the dump writes only what the game happened to draw. A
sheet is every frame of a bank in one image, side by side.

1. **Export.** OPTIONS > EXTRAS > EXPORT SPRITES (it works from the front
   end, without playing) writes a sheet for each bank that has graphics to
   `Mods/Textures/Sheets`; the log says what was written. From the command
   line, `chaotix_headless --rom ROM --frames 0 --export-sheets DIR` does the
   same (after the run, so with `--stage` and `--frames` it also gets that
   scene's banks).
   - The characters' banks 00-0f are read straight from the ROM (the game
     draws them from there as they are): fourteen sheets, as banks 01 and 09
     are empty. A ROM patch's graphics are exported as patched.
   - Other banks (objects, the HUD, the title) are compressed in the ROM and
     unpacked into SDRAM when a scene starts, so only the ones the scene
     running at that moment has loaded can be exported, from memory; they
     come along with the characters' (export from the pause menu in a level
     to get that level's). Their colours are the 32X palette at that moment,
     with no colour offset.
2. **Edit.** Copy a sheet and its `.txt` into `Mods/Textures` and edit the
   PNG in an editor that keeps it palette-indexed, as with single files.
3. **Load.** Each edited cell of a graphic replaces that frame, exactly as a
   `32x_<bank>_<frame>.png` of the cell would. A cell left as exported is not
   loaded at all (the description holds a hash of each cell as exported), so
   the game draws its own graphic there: a sheet loaded unchanged replaces
   nothing and costs nothing. A frame's own file in `Mods/Textures`
   overrides its cell, so a sheet and a few single files can be mixed.

**The layout.** Cells sit sixteen to a row, so frame `0xRC` is in row R,
column C (frame 0x51 is the second cell of the sixth row). All cells of a
sheet are one size, the union of the bank's boxes around the hotspot, and the
hotspot is at the same place in every cell: the frames of an animation line
up as they do in the game, and flipping between cells shows the motion. Cells
fitted to each graphic's box would make a smaller image, but they would no
longer line up, and a modder would have to work out each frame's offset.

- Index **0** is transparent, as in single files.
- Index **0xFF** (magenta) is the 1-pixel grid between cells and fills the
  cells of empty slots (`FFFFFFFF` in the bank's frame table).
- Index **0xFE** (dark slate) fills each cell outside its graphic's box: the
  room the graphic can grow into.
- Indices 1 to 0x7F are the graphic's own pixel values, as in single files.
  A character's sheet shows them in the character's colours as the player
  (pixel p in the character's colour p, read from the ROM's table at
  0x0018E4); values above 18, which some of Mighty's frames use, are greys.

0xFE and 0xFF are not values the game's graphics use (they stay below 0x80),
and both read as transparent when a sheet is loaded, so they cannot be used
as pixel values in a sheet (a single file can).

**How a cell becomes a replacement.** A cell is edited when any of its
pixels differs from the export, inside its box or not. The graphic's box (where the original
is drawn in the cell) is the replacement, the same size as the original.
Pixels painted to the right of the box or below it, inside the cell, make the
replacement larger from the same top-left corner, the way a larger single
file grows. Pixels painted to the left of the box or above it are left out,
with a line in the log: a replacement cannot grow that way. Everything else
(the colour offsets, flips, scaling, the 127-pixel limit) is as for single
files.

**The description** is lines of words; `#` starts a comment. From
`32x_00.txt` (Mighty), with one line of `32x_0a.txt`:

```
chaotix-sprite-sheet 1
bank 00
size 817 391
cell 50 64
hotspot 24 40
frame 00 1 1 50 64 -12 14 -18 19 5fd2fcb49b54da67
frame 08 409 1 50 64 empty
frame 22 95 89 46 43 same 14
```

- `size W H`: the sheet in pixels; `cell W H`: one cell; `hotspot X Y`: the
  hotspot's place in every cell, from its top-left pixel.
- `frame FF X Y W H x0 x1 y0 y1 HASH`: frame FF's cell (its top-left pixel
  X, Y in the sheet and its size), and the graphic's box around the hotspot:
  columns x0 to x1 - 1 and lines y0 to y1, as the graphic's header has them
  (lines divided by 256). The box therefore starts at (X + hotspot X + x0,
  Y + hotspot Y + y0) and is `x1 - x0` by `y1 - y0 + 1` pixels. HASH, 16
  hex digits, is the FNV-1a hash of the cell's W x H pixel indices, row by
  row, as exported. It may be left out (descriptions written before it
  existed have none): every cell of that frame is then loaded, edited or not.
- `frame FF X Y W H empty`: an empty slot.
- `frame FF X Y W H same GG`: the slot holds the same graphic as frame GG
  (Knuckles' bank 0a frames 22 and 23 repeat 14 and 15). The game's graphic
  is named by its first slot, so it is replaced through GG's cell; this cell
  shows it but is not read.

A sheet whose description is missing, malformed (an unknown entry, a frame
described twice, a box outside its cell, a cell outside the sheet), for
another bank than its name, or whose image is not the described size or not
palette-indexed is not loaded, with a line in the log saying why.

Known limits:

- A few graphics in SDRAM are rewritten by the game while the scene runs (in
  Botanic Base, the background picture of bank 51 changes its pixel values),
  so a sheet exported at one moment shows them as they were then. Left
  unedited they are not loaded and stay the game's; an edited cell of one
  freezes it, as a single file does.
- An edit that puts a cell back exactly as exported is no edit: the cell is
  not loaded.
- The editor must keep the sheet's indices: one that re-orders or merges the
  palette when saving changes the pixel values.

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
- `sprite_sheet.*` unit tests (a hand-built ROM image): a bank read with its
  empty and repeated slots and its colours; the layout, its description and
  back; every cell as exported giving back the original graphic (and, as a
  replacement, exactly the original bytes); cells growing right and down and
  not left or up; an unchanged sheet giving no replacement, one edited pixel
  exactly one, a description without hashes every cell; malformed
  descriptions and hashes, a missing description, another bank's, a
  full-colour or wrongly sized sheet refused; single files overriding cells.
- `sprite_sheets` (with the ROM): the fourteen character sheets exported
  from the ROM without playing, loaded unchanged, load no replacement and
  give exactly the original pictures in Botanic Base 1 (Knuckles and Espio,
  widescreen and 4:3) and the lobby, with lockstep holding; with the hashes
  taken out, all 969 cells load and the pictures are still exactly the
  original ones. Knuckles' sheet recoloured loads its 113 cells and changes
  the level with him in most pictures, in lockstep, and a level with Mighty
  and Espio not at all.
- By hand: the sheets exported at frame 900 of Botanic Base 1 (the 14
  character sheets and 27 of the level's SDRAM banks), loaded unchanged:
  0 replacements and the 1500 pictures of the level identical, in lockstep
  (before the hashes, bank 51's frozen cell changed 344 of them). With
  Knuckles' body and the HUD font (bank 13, from SDRAM) recoloured, 3000
  frames with random input in the level and the lobby, in widescreen and
  4:3: lockstep bit-identical, `--sprites-check` 0 mismatches, no byte of
  polygons and fills changed.
- The cost, Botanic Base 1 in widescreen with random input (about 1.45 ms a
  frame without mods): a sprite pack loaded but none of its graphics in the
  scene, about 0.08 ms a frame (recording the draws and following the frame
  buffer writes); replacing the scene's sprites, about 0.17 ms. A tile pack,
  about 0.04 ms. Nothing without a pack, or with the dump off.
