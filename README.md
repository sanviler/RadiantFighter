# RadiantFighter

**RadiantFighter** is a free, open-source port of the Amiga AGA beat-'em-up **Shadow Fighter** (N.A.P.S. Team /
Gremlin Interactive, 1994) to Windows and Linux. It runs the original game, unchanged, on a small Amiga 1200
written in C++ (68000 CPU and AGA chipset), with sharp integer scaling, configurable control ports and fullscreen.

> **No part of the original game is included in this project.** RadiantFighter contains no game code, graphics,
> sound, text, ROM or disk image. To play you need your own copy of Shadow Fighter (see [Game files](#game-files)).
> Shadow Fighter and all its contents are the property of their respective owners. The author of RadiantFighter
> claims no rights over the game. RadiantFighter is not affiliated with or endorsed by N.A.P.S. Team, Gremlin
> Interactive or any rights holder.

---

> **Made largely with AI.** Most of RadiantFighter (the analysis of the original game, the code and the
> documentation) was written by an AI assistant (Anthropic's Claude), directed, tested and reviewed by the
> author.

---

## Features

- **The original game, as it was.** The original 68000 code runs on an emulated Amiga 1200: CPU, copper, blitter,
  AGA bitplanes (up to 8 planes, 256 colours of 24 bits, BPLCON3 banks, BPLCON4, wide fetch modes), AGA sprites
  (16, 32 and 64 pixels wide), Paula audio, CIAs, keyboard and joystick ports. No Kickstart ROM is needed.
- **No disk drive, fast loading.** The game's track loader reads the disk images directly and the packed blocks
  are unpacked in C++: the Gremlin logo appears after about two seconds.
- **Sharp picture.** Full-screen pictures (title, menus, selection) are shown in 4:3 with the
  overscan area the game uses; the fight is shown as its exact display window.
  The window opens at half the desktop and can be resized.
- **Picture modes** (options menu, PICTURE): PIXEL PERFECT = the largest integer scale that fits the window or
  the desktop, black borders, with the CRT-Pixie filter computed exactly at that scale; FILL = the full height of
  the window or screen at any scale, with sharp pixels (sharp bilinear: no uneven pixels) or a CRT filter made
  for non-integer scales (every scanline gets the same light). Both start with their CRT filter on; F5 switches
  it off and on. Windows display scaling (125%, 150%) no longer blurs or shrinks the picture.
- **CD or Amiga music, your choice.** MUSIC: CD plays the audio tracks of the CD32 version (WAV files you make
  from your own disc, see below), as on the CD32; Paula stays free, so every fight has the stage (background)
  sounds and the sound effects at the same time. MUSIC: PAULA is the original Amiga music and sound, untouched.
  Never both at once. Separate volumes for the music and for the sound effects / stage sounds.
- **Move list** in the options menu: the special moves of every fighter.
- **CRT-Pixie filter** (F5): scanlines, aperture mask, glow, vignette, in the style of the crt-pixie shader.
- **Skippable intro**: Space, Return, Right Ctrl or fire (A) on a controller go straight to the title screen.
- **High scores kept** in `RadiantFighter.highs` next to the executable (same format as the WHDLoad install:
  a `ShadowFighter.highs` from WHDLoad is read too).
- The manual protection is answered for you.
- Both Amiga control ports configurable: keyboard, mouse or any joystick or game controller, for two players.

## Game files

Put next to the executable either:

- the four ADF images of the AGA floppy version (`... (Disk 1 of 4) ... .adf` and so on), or
- the WHDLoad installation folder (`ShadowFighterAGA`, with `Disk.1` ... `Disk.4`), or its four `Disk.N` files.

They are recognised by their SHA-256 digests (other versions are tried, with a warning). Another folder can be set
with `game_path=<folder>` in `radiantfighter.ini` or `--data <folder>`; if nothing is found, RadiantFighter opens
the system's folder picker (on Linux it needs `zenity` or `kdialog`).

### CD audio (optional)

RadiantFighter creates an `audio` folder next to the executable. Extract the audio tracks of your Shadow Fighter
CD32 disc (or of a dump of it) as WAV files, 44.1 kHz, 16-bit, stereo, and copy them there with exactly these
names:

| File | Disc track |
|---|---|
| `track02.wav` | first audio track (track 1 of the disc is the game data) |
| `track03.wav` ... `track12.wav` | the following tracks |
| `track13.wav` | last audio track used by the game |

The game plays track 3 in the menus and the selection, a track per fight (as the CD32 version does) and track 13
in the ending. Any WAV with these names plays, your own music too. The folder also holds a `README.txt` with these
instructions. With the folder empty, or MUSIC set to PAULA in the options menu, the original Amiga music plays.
The intro keeps its Amiga music in both modes (the CD32 intro has no CD track).

## Controls

| Key | Action |
|---|---|
| Joystick in port 2 | player 1 (as on the Amiga) |
| Joystick in port 1 | player 2 |
| Keyboard joystick | numeric keypad 8 4 6 2 (7 9 1 3 diagonals), Right Ctrl = fire |
| Other keys | the Amiga keyboard (as in the original: for example P pauses the fight) |
| Escape | options menu (the game is paused) |
| F5 | CRT filter on / off |
| Space / Return / Right Ctrl / A | skip the intro |
| F11 | window / fullscreen |
| F12 | screenshot (PNG next to the executable) |
| Game controller | stick or d-pad = joystick, A = fire, Start = options menu |

The options menu sets: fullscreen, the picture (pixel perfect or fill), the CRT filter, music (CD or Paula), music volume, effects volume, the device in
each control port (nothing, keyboard, mouse or any joystick); it shows the move list (left / right: fighter) and
can restart the game or quit. The settings are kept in `radiantfighter.ini`.
With MUSIC: CD the SOUND item of the game's own options menu has no effect (stage sounds, effects and CD music
always); with MUSIC: PAULA it works as in the original.

One fire button, as in the original (the CD32 version too: its pad buttons are used only for pause, menu choices
and a cheat). Holding fire repeats the basic attack: this is the original behaviour (the game reads the button
state every frame), and some special moves are "rapid fire" moves.

## Cheat codes

From the WHDLoad install of Psygore & Codetapper (codes found by Keith Krellwitz, Sweet Cheater); they work in
RadiantFighter as in the original. On the options screen type only one of these (the last one typed counts):

| Code | Effect |
|---|---|
| `MBARIVIDISOCCAFFARIMBARI` | play as Shadow Fighter |
| `PARAPONZIPONZIPO` | play as Pupazz |
| `TIRIKAKKU` | infinite credits |

During a fight press P (pause) and type:

| Code | Effect |
|---|---|
| `EBBRAVOSCECCU` | win the current round |
| `CUNIGGHIUACCURRENNU` | speed up the game |
| `SURICIMONCU` | normal speed |

The CD32 version has one more, with the pad of port 2 during the pause: green, yellow, green, yellow, green,
yellow, blue, yellow, blue, yellow, blue, green, green (CD32 only: not available in RadiantFighter).

## Command line

```
radiantfighter [--data <folder>] [--fullscreen] [--test-frames N]
```

## Building

C++20 and SDL2. The Amiga core (68000 CPU from [Moira](https://github.com/dirkwhoffmann/Moira) by Dirk W. Hoffmann,
MIT licence, and the chipset) is in `core/`.

- **Windows** (Visual Studio 2022 + vcpkg): `cmake --preset windows-vcpkg` then `cmake --build --preset release`;
  the result is a single `radiantfighter.exe` with SDL2 linked in. `scripts/package.ps1` makes the zip.
- **Linux / macOS**: install SDL2 (`libsdl2-dev`, `brew install sdl2`), then `cmake --preset linux` and
  `cmake --build build -j`. `-DRF_LINUX_PORTABLE=ON` makes an executable for other distributions (glibc 2.34+).
- `-DRF_BUILD_TOOLS=ON` adds the developer tools: `rfrun` (the game without a window: screenshots, keys,
  joystick scripts, random input), `agatest` (self-test of the AGA chipset) and `rf_formats` (data converters).

The GitHub workflow builds Windows, Linux and macOS at every push and attaches the packages to a release for every
`v*` tag.

## Roadmap

- The CD32 joypad (seven buttons).
- A true 16:9 fight view: it needs the stage drawn in C++ beyond the graphics the game provides.
- More of the original routines translated to C++.
- macOS and Android packages: open to the community (the code builds on macOS; nobody tests it yet).

## Licence

RadiantFighter is © Lorenzo Castelletta and released under the **GNU General Public License, version 3 or later**
(see `LICENSE`). Moira (in `core/ext/moira`) is under the MIT licence.
