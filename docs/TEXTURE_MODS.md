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

**The Mega Drive layer can be replaced now** (below). The 32X layer is drawn
by the SH-2 in software, straight into its frame buffer; replacing it needs
the sprite routines to be found first and is not done yet. For both layers,
editing the ROM's graphics with existing tools and shipping a
[ROM patch](ROM_PATCHES.md) also works.

## Mega Drive tiles

1. Turn on OPTIONS > EXTRAS > DUMP TILES (`DumpTiles = true` in `[Mods]`) and
   play the part you want to change. Each 8x8 tile the game draws is saved
   once to `Mods/Textures/Dump` as `md_<hash>.png`: a palette-indexed PNG
   with the palette the game drew it with. Turn it off again when done.
2. Copy the tiles you want to change into `Mods/Textures`, keeping their names,
   and edit them in an editor that keeps the image indexed (Aseprite, GIMP in
   Indexed mode, ...). Keep them 8x8.
3. Start the game: each `md_<hash>.png` in `Mods/Textures` is drawn instead of
   the tile it is named after, wherever and whenever the game uses it.

How it behaves:

- A tile is recognised by its pixels, not its place in video memory, so it is
  replaced wherever the game uploads it.
- The PNG's colours are palette indices: 1-15 are the 15 colours of the tile's
  palette line, 0 is transparent. They are coloured with the palette in use as
  the tile is drawn, so the game's fades and palette cycling still apply.
  Changing which colours a palette line holds is a ROM patch's job.
- The same tile drawn with a different palette line uses the same replacement,
  in that line's colours.
- Only the picture changes. The game's view of the VDP (the sprite collision
  and overflow flags in particular) still comes from the original tiles, so
  the game behaves as it would without the pack.

Not done: larger (high-resolution) replacements, and full-colour PNGs (they
could not follow the game's palette).

## Validation

- `tile_replace.*` unit tests: sprite and plane tiles replaced, transparency,
  the collision flag following the original tiles both ways, dumping once with
  the palette.
- `image_io.*` unit tests: the PNG reader (all colour types, bit depths,
  filters, fixed and dynamic Huffman, damaged and unsupported files) and the
  indexed writer.
- `tile_pack` (with the ROM): Botanic Base's dumped tiles loaded back as a pack
  give exactly the original pictures, and the recompiled machine stays
  bit-identical to the interpreter (lockstep, random input). A pack with
  every tile's colours swapped changes the backgrounds and leaves the 32X
  layer alone, with lockstep holding over 1500 frames.
- The cost: about 0.04 ms a frame with a pack loaded, nothing without one.
