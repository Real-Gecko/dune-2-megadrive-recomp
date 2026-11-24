# Dune - The Battle for Arrakis for Windows and Linux

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

A native PC port of **Dune - The Battle for Arrakis**, the 1993 real-time strategy game by
Westwood Studios for the Sega Mega Drive / Genesis. The game's 68000 code was translated into
C++ and runs on SDL3. It is not an emulator: there is no emulated console at run time.

## Your own ROM

The game is not freeware, so no game data is included. You need your own dump of
*Dune - The Battle for Arrakis (USA)* (1 MB, CRC32 `4dea40ba`). On the first start the port
asks for it and extracts the game data from it into `data/` next to the
executable. After that the ROM is no longer needed.

The data is kept as files you can open: graphics as PNG images (`gfx/`) and palettes
(`palettes/`), screen layouts (`maps/`) and text (`text/`, `files/*.INI`) as text files, sound
samples as WAV files (`sfx/`). Music, scripts and tables stay binary (`bin/`, `files/`,
`anims/`). Edited files are loaded as long as they keep their size: the same number of tiles,
text the same length, samples the same length (INI files may get shorter).

## What the port does

- Runs the original game logic, so missions, units and AI behave as on the console.
- Plays the music on an emulated YM2612 sound chip, as the console does.
- Adds a wide battle view.
- Adds mouse control: click to select and order, drag to select several units,
  Shift+click / Shift+drag to add or drop units, click the radar to move the view.
- Adds control groups: Ctrl+digit stores, digit selects (twice: centres the view),
  Shift+digit adds.
- Adds repair mode (R), sell mode (Q), and Space to centre the view on the last event.
- Adds save games.

## Download

Get the zip for your system from [Releases](../../releases):

- **Windows:** `dune2-<version>-Windows-x64.zip`. Unpack it and run `dune2.exe`.
- **Linux:** `dune2-<version>-Linux-x64.tar.gz`. Install SDL3 from your distribution, unpack, and run `./dune2`.

## Controls

| Pad   | Keyboard | Gamepad | Mouse |
| --- | --- | --- | --- |
| D-pad | Arrows | D-pad / left stick | |
| A | Enter | West | Left button |
| B | | South | Right button |
| C | Shift (battle) | East | |
| Start | | Start | |

Esc opens the options in battle and closes menus. Alt+Enter toggles fullscreen (the game
starts fullscreen).

Saves go to `saves.txt` next to the executable.

## Building

Needs CMake 3.20+, a C++20 compiler and SDL3.

```sh
make        # Linux: ./dune2
make win    # Windows cross-build with mingw-w64: dune2.exe and SDL3.dll
```

`make win` downloads the official SDL3 Windows release into `build/win-deps`.

## License

The project has parts with different owners:

- **Port code:** the files in `src/` outside `src/game/` and `src/ymfm/`, and the build files are under the [MIT License](https://opensource.org/license/mit).
- **Game code:** `src/game/` is translated from the original Dune - The Battle for Arrakis and
  stays © its original owners (Westwood Studios, Virgin Interactive).
- **Game data:** not included; it is extracted from your own ROM and stays © its original owners.
- **Libraries:** `src/ymfm/` is [ymfm](https://github.com/aaronsgiles/ymfm) by Aaron Giles
  (BSD-3-Clause). The Windows release includes SDL3 (zlib license).
