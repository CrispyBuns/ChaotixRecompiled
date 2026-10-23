# Third-party components

What this repository redistributes, and what it only depends on. None of it
is game data; see [LEGAL.md](LEGAL.md) for that.

## Included in this repository

### Inter (font)

- Files: `assets/fonts/Inter-Regular.ttf`, `assets/fonts/Inter-SemiBold.ttf`
- Copyright (c) 2016 The Inter Project Authors, <https://github.com/rsms/inter>
- Licence: SIL Open Font License 1.1 — the full text is in
  [`assets/fonts/OFL-Inter.txt`](assets/fonts/OFL-Inter.txt)

Inter draws the first-run setup screen.

### Press Start 2P (font)

- File: `assets/fonts/PressStart2P-Regular.ttf`
- Copyright (c) 2012 The Press Start 2P Project Authors, <https://zone38.net>
- Licence: SIL Open Font License 1.1 — the full text is in
  [`assets/fonts/OFL-PressStart2P.txt`](assets/fonts/OFL-PressStart2P.txt)

Press Start 2P draws the in-game menu, where a pixel face suits the game it
is drawn over.

Both are compiled into the executable (see `cmake/embed_file.cmake`), because
those screens have to work before any asset folder has been found — the setup
screen is what runs when nothing is installed yet.

### App icon

- Files: `assets/icon/icon.png`, `assets/icon/icon_64.rgba`,
  `platforms/windows/app.ico`, `platforms/android/app/src/main/res/mipmap-*/ic_launcher.png`
  (the macOS and iOS icons are scaled from `icon.png` when they are packaged)
- The Knuckles' Chaotix logo by Sega / Sonic Team, from
  [Wikimedia Commons](https://commons.wikimedia.org/wiki/File:Knuckles%27_Chaotix_logo.webp),
  where it is marked public domain as a text-only logo (PD-textlogo). It may
  still be a trademark of Sega; see [LEGAL.md](LEGAL.md). The logo is
  unchanged; the red rounded square behind it was drawn for the icon, and
  the sizes are scaled from a 1024 px master.
- Up to 0.6.1 the icon was pixel art of Knuckles by Cotton_Candy_2C, from
  [SteamGridDB](https://www.steamgriddb.com/icon/22213), suggested by
  [Abdouxgo](https://github.com/Abdouxgo) in
  [issue #22](https://github.com/YuutaTsubasa/ChaotixRecompiled/issues/22).

The OFL permits redistribution, bundled or embedded, as long as the licence
travels with it and the font is not sold on its own. All three files are the
unmodified released ones.

## Required, not included

Fetched or installed separately; nothing from them is committed here.

| Component | Licence | Used for |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) ≥ 3.2 | Zlib | window, rendering, input, audio, file dialogs |
| [SDL_ttf](https://github.com/libsdl-org/SDL_ttf) ≥ 3.2 | Zlib | text rendering for the setup and achievement screens |
| FreeType, HarfBuzz, plutosvg, plutovg | FTL / MIT / MIT / MIT | vendored inside SDL_ttf when it is built from source |

On a desktop the two SDL libraries come from a package manager
(`mingw-w64-x86_64-sdl3`, `mingw-w64-x86_64-sdl3-ttf`, or the equivalent).
For Android, macOS, iOS and CI they are built from a source checkout — see
`CHAOTIX_SDL3_SOURCE_DIR` and `CHAOTIX_SDL3_TTF_SOURCE_DIR` in the build
files, [platforms/android/README.md](platforms/android/README.md) and
[docs/BUILDING_APPLE.md](docs/BUILDING_APPLE.md). The macOS and iOS builds
link all of them into the program; their packages carry the licence texts in
`licenses/` (`scripts/package_apple.sh` copies them from the source trees).
