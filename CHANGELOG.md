# Changelog

## 1.0.0

- Options panel: the same fixed size in every port (256 x 200 pixels of its own, at an integer scale of its own),
  drawn over the picture and never through the CRT filter. Long values and footers (for example the name of a
  controller) are split on more lines instead of widening the panel; only if the text needs more rows the panel
  grows downwards.
- Faster picture filters: the worker threads are made once instead of every frame, the glow is computed in
  parallel, and the CRT filter of FILL computes the beams on the source columns (about half the time before,
  same look); the beam table of FILL is kept per picture.

## 0.7.0

- Picture modes: PIXEL PERFECT (largest integer scale, CRT-Pixie) and FILL (full height at any scale: sharp
  bilinear pixels or a CRT filter for non-integer scales, with the same light on every scanline). Both start with
  the CRT filter on (F5 switches it). Settings `picture` and `crt` in `radiantfighter.ini`. At 1080p the
  full-screen pictures (272 lines) were at 3x (76% of the height): FILL uses the whole height.
- Windows with display scaling (125%, 150%): the picture uses the real pixels of the screen (before, Windows
  stretched a smaller window: smaller and blurred picture).
- The intro can be skipped with Space, Return, Right Ctrl or A on a controller.
- Window title: name, version, game and frames per second.

## 0.6.2

- Music: CD or Paula (options menu), never both: with CD the menus start the CD track at once (no more seconds
  of Amiga music before it); with Paula the original music and the game's SOUND option. CD chosen without tracks
  = Paula.
- Music volume and effects volume (sound effects and stage sounds), also for the Paula music.
- The "audio" folder gets a README.txt that explains which tracks go there and their exact names.
- Move list in the options menu: the special moves of every fighter (from the list by Dracs, 1994).
- README: cheat codes, notes on the fire button.

## 0.6.1

- Picture always scaled by an integer factor (of the desktop resolution in fullscreen, of the window otherwise),
  centred with black borders: every pixel and every CRT-Pixie scanline has the same size (non-integer scaling
  dropped rows and left irregular dark lines).
- Window: half the width and height of the desktop at start, resizable; the picture keeps the integer scale.
- CRT-Pixie computed exactly at the output scale (no upper limit), then copied 1:1.

## 0.6.0

- Fight picture in the right place: with the AGA wide fetch the first pixel is at DDFSTRT (not at DDFSTRT aligned to
  the fetch unit) and the fetch lasts one more unit; fighters and panel were 16 pixels to the left.
- Picture: full-screen pictures in 4:3 (overscan area of the game), the fight as its exact display window, scaled
  until the height reaches the edges. The border option and the wide fight view are gone.
- Sound: music only from the CD tracks (track02.wav ... track13.wav in the "audio" folder next to the executable,
  created by the port): menus track 3, fights their CD32 track, ending track 13; the fights always have the stage
  sounds and the sound effects; the Paula music is not played.
- Options menu: fullscreen, CRT-Pixie (F5), CD audio on/off, control ports, restart, quit.

## 0.5.0

- Picture: the full overscan area that the game uses (352x272 from colour clock $71, line $22) instead of a fixed
  320x256 window: the selection screen is no longer cut and the fight is no longer shifted.
- Loading: the RNC unpacker runs in C++ (the black screen before the Gremlin logo goes from about 12 to 2 seconds).
- CD audio from the CD32 tracks (cue/bin or WAV): fight music from the CD; with SOUND = BACKGROUND the game plays
  CD music, stage sounds and sound effects together.
- Wide fights: the display window is opened to the graphics the game draws at the sides (352 instead of 316).
- CRT filter (crt-pixie style), F5 or options menu.
- High scores saved in RadiantFighter.highs next to the executable (WHDLoad ShadowFighter.highs read too).
- Core: AGA wide window, framebuffer from colour clock $50, CRT filter in the video layer.

## 0.1.0

- First version: Shadow Fighter AGA runs on the emulated Amiga 1200 from the user's disk images (WHDLoad Disk.1-4
  or the four ADF files): intro, menus, fighter selection, fights.
- AGA chipset in the core: 2 MB chip RAM, 8 bitplanes, 256 colours of 24 bits (BPLCON3 bank and LOCT),
  BPLCON4 (bitplane XOR, sprite colour banks), FMODE fetch widths for bitplanes and sprites, HAM8, KILLEHB,
  dual playfield colour offset, extended scroll, Lisa/Alice identification.
- Track loader replaced by reads from the disk images; manual protection answered; optional intro skip.
- Options menu: fullscreen, border, control ports (keyboard, mouse, joysticks, game controllers), intro.
