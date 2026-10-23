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

## Mega Drive text by tile sequence: `mdtext_<name>.txt` and `mdtext_<name>.png`

The menus' text on the Mega Drive layer (the title menu's SCENARIO QUEST /
TRAINING / OPTIONS, the OPTIONS screen, the DATA LOAD banners, PLAYER SELECT's
names in the 8x16 font) is written with a font: one tile per letter (two
stacked for the 8x16 font), shared by every string. Replacing the "O" tile
with an `md_` file changes every O. A translation needs to replace whole
words, so a text pattern names a **run of tiles**, in order, and gives an
image drawn over that run wherever a plane shows it, and nowhere else.

### Finding the run

With DUMP GRAPHICS on (or `chaotix_headless --dump-graphics DIR`), each
screen whose plane text is new is also listed in `Mods/Textures/Dump/Text`
(`DIR/Text`): `text_<n>.txt`, the runs of tiles each plane shows, and
`text_<n>.png`, the screen at twice the size with each run boxed and
numbered as in the list (yellow plane A, cyan plane B, magenta the window).
A screen is listed once its new runs have stayed in place for 10 pictures,
at most every 30 pictures, so menus are listed and a scrolling level is
not listed every picture (`<n>` is a picture counter). From the title menu:

```
# run 3: screen x 128 y 152, 7 tiles (56x8 pixels), plane cell 16,19, palette line 1
plane A
row c55f51f079e83c63 c47e6d034eb92447 61289eb94ebec2bf ee6410f28116fbcf c55f51f079e83c63 06e89b91ed8a5c67 262ed75e7586890f
```

That is OPTIONS: the first and fifth tiles are the same O. A run is a row of
non-blank tiles; one blank tile inside it (a space between words) is kept,
two end it. For the 8x16 font the list says which run lies right below
another at the same place and width: the letters' top and bottom halves.

### The pattern

Copy the run's lines into `Mods/Textures/mdtext_<name>.txt` (any name;
`mdtext_options.txt`):

```
# OPTIONS on the title menu
plane A
align center
row c55f51f079e83c63 c47e6d034eb92447 61289eb94ebec2bf ee6410f28116fbcf c55f51f079e83c63 06e89b91ed8a5c67 262ed75e7586890f
```

- `row`: one row of tiles, left to right, as the dump writes them: the tile's
  hash (the same as `md_<hash>.png`'s) and, for a flipped tile, `:h`, `:v`
  or `:hv`. A flipped tile only matches flipped the same way. Several `row`
  lines make a pattern several tiles high (two for the 8x16 font, top row
  first); every row has the same number of tiles.
- `plane A`, `B`, `W` (the window) or `any` (the default); several can be
  listed.
- `palette 0`-`3` matches the run only when its tiles are drawn with that
  palette line; `any` (the default) matches whatever the line. On the title
  menu the chosen entry is drawn with line 0 and the others with line 1, so
  one pattern without `palette` covers both, and two with `palette 0` and
  `palette 1` can draw them differently.
- `align left` (the default), `center` or `right`: where the image sits when
  it is not as wide as the run.
- `#` or `;` starts a comment.

### The image

`mdtext_<name>.png`, next to the `.txt`:

- **Palette-indexed**, like the `md_` tiles: indices 1-15 are the colours of
  the palette line the run's **first tile** is drawn with, 0 is transparent.
  So the game's fades and palette changes (the highlighted menu entry) still
  apply. Full-colour PNGs are refused.
- **8 pixels high per row** of the pattern (16 for the 8x16 font).
- **Any width** (1 to 1024 pixels). The run is `8 x tiles` pixels wide (56
  for OPTIONS). An image of that width is drawn exactly over it. A narrower
  one is placed by `align`, the rest of the run is transparent. A wider one
  is placed by `align` too and reaches over the tiles beside the run: there
  its pixel 0 leaves the plane's own pixels showing, others cover them.
- Inside the run the image replaces the tiles entirely: its 0 pixels show
  what is behind the plane (plane B, the backdrop, the 32X layer), as a
  tile's 0 pixels do. The run's tiles' `md_` replacements do not apply.
- The image takes the first tile's palette line and priority bit as they
  are when each line is drawn, and is drawn as it is (a flipped run's image
  is not flipped).

### How it is matched

Before a picture is drawn, the planes' name tables are searched for the
patterns (indexed by their first tile, so the cost is small; nothing is done
without a pattern). A match is a place in the plane, not on the screen, so
it scrolls with the plane, by line or by column, as its tiles do. Where
matches overlap the one with the most tiles wins (then the one whose file
name sorts first).

The replacement follows what each line actually shows. Before a line is
drawn, the name table rows it shows, and the rows above and below that a
pattern on them could span, are compared with the rows as last matched; if
any changed, the plane is searched again, with the name table as it is at
that line. So:

- Text the game writes into the name table in the middle of a picture is
  replaced from the line it shows on.
- Rows the game clears and writes again before the beam reaches them stay
  replaced: when DATA LOAD's slot panel starts and stops sliding, the game
  clears the panel's rows and rewrites them during the picture (0.7.0 showed
  the English NO SAVE / DATA 1 for that one picture).
- A change the screen shows is shown: a run whose tiles change while its
  rows are drawn shows its tiles from that line on (or another pattern's
  image, if the new tiles match one). A pattern several rows high is
  checked whole, so a change in its lower rows made before they are drawn
  already shows on the lines of its upper rows drawn after the change.
- A change of palette line or priority alone keeps the image, in the first
  tile's new colours (DATA LOAD dims and lights its slots that way while a
  picture is drawn).

Checking costs a comparison of a few rows per line, and only when a pattern
is loaded. Like the other replacements this only changes the picture: the
machine runs as without it, bit-identical in lockstep.

Known limits:

- Text on the 32X layer (the title's PUSH START and copyright, the HUD) is
  not on a plane; it is replaced as [32X sprites](#32x-sprites-32x_bank_framepng-and-32x_hashpng).
  Text drawn with sprites of the Mega Drive VDP is replaced by
  [sprite](#mega-drive-text-by-sprite-mdsprite_nametxt-and-mdsprite_namepng).
- A pattern matches whole tiles. Text that the game draws at different tile
  offsets, or that changes one tile (a digit in DATA 1 / DATA 2), needs a
  pattern per variant (DATA 1, DATA 2, ...).
- Tiles are recognised by their pixels as they are when the picture
  starts: a letter tile the game uploads in the middle of a picture is
  recognised from the next one.
- The dump lists the native 320-pixel screen, not the widescreen margins
  (patterns match there too), and samples each tile row in its middle: on a
  plane scrolled by a fraction of a tile per line the listed run can differ.

## Mega Drive text by sprite: `mdsprite_<name>.txt` and `mdsprite_<name>.png`

Some of the menus' Mega Drive text is drawn with the VDP's **sprites**, not
on a plane: the big banners' letters (DATA LOAD, PLAY, DELETE, TRAINING,
EXIT, PLAY STYLE, PLAYER SELECT; their blue ovals are plane tiles), PLAY
STYLE's 1P / 2P / CPU, the SAVEMODE AUTO / MANUAL box, the 8x16 font when the
game writes it with sprites (TRAINING's zone names, PLAYER SELECT's header
and names, see [MENU_ART.md](MENU_ART.md)) and SOUND TEST's labels and
titles. Their tiles are shared too: DATA LOAD is four sprites of 4x3 tiles,
the letters straddle them, and the fourth sprite starts with the first one's
tiles (the D). An `mdsprite_` pattern names **the sprites**, by their tiles,
and draws an image instead of them, only where they appear together.

### Finding the sprites

The same DUMP GRAPHICS (`--dump-graphics DIR`) also writes
`Dump/Text/sprites_<n>.txt` with `sprites_<n>.png` (green boxes): the
sprites on screen, grouped into suggested patterns -- sprites on the same
line (same Y and height) with the same palette line, side by side (at most 8
pixels apart). Listed as the plane text is: once a new group has stayed in
place for 10 pictures, at most every 30. From DATA LOAD, the PLAY banner:

```
# group 10: screen x 32 y 182, 2 sprites, 21 tiles, 56x24 pixels, palette line 0, priority 1
#   entry 0 (list place 0) at 32,182, tile 28f
#   entry 1 (list place 1) at 64,182, tile 29b
sprite 0 0 4x3 c201ca0c81c8217a ec2112a45d4c3071 dbeeab7d6f977c77 75aca8175f4592c7 3ab6a123744c790b 5e0782ad57085e03 d4f66b2be90e7ce3 2f99af2edff6ac82 4c0fb2ca7a75840b 9d41d178c17f0d61 56d7fa498591f3bf dbeeab7d6f977c77
sprite 32 0 3x3 cd9d0c4658f2da84 501a169d4c874f8f dbeeab7d6f977c77 2a3877ebf5817edb 262473a08d4ee92c 131ce9dedb1cb833 bf31e57f3efae0a6 171bb79dc8e18503 171bb79dc8e18503
```

A suggestion is only that: a group can hold a sprite that is not part of
the text (drop its line) or miss one on another line (copy its line from its
own group, with its offset: the `#` lines give each sprite's place on
screen).

### The pattern

`Mods/Textures/mdsprite_<name>.txt` (any name; `mdsprite_play.txt`):

```
# PLAY on DATA LOAD
align center
sprite 0 0 4x3 c201ca0c81c8217a ec2112a45d4c3071 dbeeab7d6f977c77 75aca8175f4592c7 3ab6a123744c790b 5e0782ad57085e03 d4f66b2be90e7ce3 2f99af2edff6ac82 4c0fb2ca7a75840b 9d41d178c17f0d61 56d7fa498591f3bf dbeeab7d6f977c77
sprite 32 0 3x3 cd9d0c4658f2da84 501a169d4c874f8f dbeeab7d6f977c77 2a3877ebf5817edb 262473a08d4ee92c 131ce9dedb1cb833 bf31e57f3efae0a6 171bb79dc8e18503 171bb79dc8e18503
```

- `sprite DX DY WxH TILE...`: one sprite. `DX DY` is where it is, in
  pixels; only the distances between the sprites count (the first line is
  the anchor, so screen coordinates work as well as the dump's offsets).
  `WxH` is its size in tiles (1-4 each way), with `:h`, `:v` or `:hv` after
  it for a flipped sprite (it then only matches flipped the same way). Then
  its W x H tiles' hashes in the order the VDP stores them, **column by
  column** (top to bottom, then the next column), as the dump writes them,
  whatever the flip.
- `palette 0`-`3`: matches only when the first sprite is drawn with that
  palette line; `any` (the default) matches whatever the line.
- `align left` (the default), `center` or `right`: where the image sits
  across the sprites' bounding box when it is not as wide.
- `offset X Y`: moves the image by that many pixels (negative: left or up).
- `#` or `;` starts a comment.

### The image

`mdsprite_<name>.png`, next to the `.txt`:

- **Palette-indexed**: indices 1-15 are the colours of the **first
  sprite's** palette line (as it is when each line is drawn; the first
  sprite present, when the group is partly off screen), 0 is transparent.
  Full-colour PNGs are refused.
- **Any size** (up to 1024 x 512). Its top-left is the sprites' bounding
  box's top-left (moved by `align` and `offset`): an image of the box's size
  (56x24 for PLAY) covers the sprites exactly; a larger one reaches past
  them, over whatever is there.
- The matched sprites are not drawn at all, so where the image is 0 what is
  behind them shows (the oval, the background).
- The image is drawn as one sprite in the **first sprite's place** in the
  sprite list (the first sprite present's): in front of the sprites after
  it in the list and behind the sprites before it, and in front of or
  behind the planes as that sprite's priority bit says. It is not flipped.

### How it is matched

Once a picture, before it is drawn, the sprite table is followed along its
links, as the VDP does. A match is a sprite showing one line's tiles, size
and flip with, for each other line, a sprite showing its tiles exactly that
many pixels away (on screen, not in the list: the game may list them in any
order). Distances are measured as the VDP places sprites: its X is 9 bits,
so a sprite placed past X 383 shows 512 pixels further left, and a member is
looked for where the VDP would show it.

A line may also have **no sprite** when that sprite would lie wholly off
the screen: the game leaves out of the list the sprites beyond the screen's
edge, so while a banner slides in or out (TRAINING leaving to the left as
PLAY STYLE comes in from the right) only the sprites on screen are listed.
The group still matches from the sprites that are there, as long as at
least one is and each missing one would be wholly outside the picture:
beyond the native screen and the widescreen margins where these show game
content (a level's margins; the black bars beside the menus do not count).
The image is placed from a sprite that is there and cut at the screen's
edge like any sprite, so a banner's image slides with it; patterns for a
banner's first or last sprites alone are not needed.

Where matches share sprites the one with the most tiles present wins (then
a whole group over one with sprites missing, then the pattern whose file
name sorts first), so a pattern for one banner can be followed by patterns
for its sprites alone without them breaking it. Patterns are indexed by
each line's first tile; nothing is done without a pattern.

While the picture is drawn, a matched group whose sprites move or change
tiles is drawn as it is from that line on (its original sprites, no image);
a change of palette line or priority keeps the image. The status flags the
game reads (sprite collision, overflow) still come from the original
sprites, and the machine runs as without the mod, bit-identical in lockstep.

Known limits:

- A group is matched on one picture's table. Text the game draws with its
  sprites on alternate pictures (flickering, multiplexed), or whose sprites
  are placed one picture apart, matches on the pictures where the whole
  group is there (or only off-screen sprites are missing); on others the
  English sprites show.
- The image is not cut by sprite masking (an X=0 sprite) or by the VDP's
  sprites-per-line limit, which would have cut the original sprites, and it
  does not count towards them (the flags follow the originals).
- A sprite listed twice in a pattern, or two groups sharing a sprite, are not
  both drawn: one sprite belongs to one match.
- Each pattern names exact tiles: a letter whose tiles the game animates (a
  blinking cursor in the text) needs a pattern per frame of the animation,
  and the same word at another distance (PLAYER SELECT's letters spaced
  differently) another pattern.
- The dump lists the native 320-pixel screen; widescreen margins match too.

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
- `text_replace.*` unit tests: a run drawn from the image and only there;
  flipped tiles matched by their flip; the image scrolling with the plane;
  the longest pattern winning; a wider centred image covering its
  neighbours where it is not 0; the first tile's palette line, and a
  required one; a cell changed after the match; the window plane; malformed
  patterns; the tokens; the dump listing the runs once, and a moving run only
  once it stops.
- `sprite_text.*` unit tests: a sprite drawn from the image and only
  there (another size of the same tiles not); the image's size, alignment
  and offset; flips matched by their flip; a group matching only at its
  distances, with offsets in any coordinates; the group with the most tiles
  winning; the image in its first member's place among the sprites; the
  collision flag following the original sprites; a palette change during
  the picture keeping the image and a move bringing the sprites back; a
  required palette line; malformed patterns and size tokens; no change
  without a match; the dump's groups, listed once.
- `md_sprite_text` (with the ROM): DATA LOAD's banner letters found in the
  dump (four 4x3 sprites) and given a synthetic solid image change the
  picture inside the banner's box only, the title not at all, in lockstep; a
  pattern of one of its sprites loses to the group; a narrower centred image
  colours only its columns; no pattern, or one matching nothing, leaves the
  pictures byte-identical; malformed patterns and full-colour images are
  refused with a message.
- `md_text` (with the ROM): the title menu's OPTIONS run found in the dump
  and given a synthetic solid image changes the menu picture inside its box
  only, the title before the menu not at all, in lockstep; a longer pattern
  wins over a shorter one; a wider centred image reaches past the run; no
  pattern, or one matching nothing, leaves the pictures byte-identical;
  malformed patterns are refused with a message. On DATA LOAD, switching
  slots down and back with the slot's NEW given a solid image: every picture
  of the 261 has a NEW replaced in full (0.7.0 showed the English one in two
  of them, where the palette line changed during the picture).
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
