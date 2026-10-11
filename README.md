# Another World 32X

This is a port of Eric Chahi's **Another World** (1991; *Out of This World* in North America) to the
**Sega 32X**. It plays the original game from the original DOS data files, using Gregory Montoir's **rawgl**
engine, and it runs as a 4 MB 32X cartridge ROM (tested in the Fusion emulator).

## Why this exists

**This is an exercise, not an improvement.** The point of this project was to practise building a
Sega 32X game from source code that is already available: taking an existing engine (rawgl), making it
run on the 32X's two SH2 processors, and checking the result against the original.

**The game gains nothing from running on the 32X.** It does not look, sound or play better than on a PC,
an Amiga or any other version. Nothing has been added, improved or remastered: same graphics (the original
320×200, 16-colour look), same sounds and music, same levels, same passwords. If you just want to play
Another World, any of the existing versions is at least as good. The only differences from the PC version
come from the hardware, and they are small losses rather than gains: sound is mono, and the game is played
with a pad instead of a keyboard.

The game data is **not** included. You need your own copy of the DOS version.

## Credits

| Role | Name |
|---|---|
| Another World (game, graphics, design) | Eric Chahi |
| Music and sound for Another World | Jean-François Freitas |
| rawgl engine (the game engine reimplementation this port runs) | Gregory Montoir |
| D32XR 32X code (pad reading, Sega CD support code used by the 68000 program) | Victor Luchits |
| Sega CD and 32X framework | Chilly Willy |
| Sega 32X port | Micronut99 |

The port's 32X platform layer, start-up code and 68000 cartridge program come from Micronut99's earlier
32X ports (Kobo Deluxe and Tyrian 2.1), which are themselves built on the D32XR and Chilly Willy code above.
The same credits appear on screen when the ROM starts.

All rights to Another World remain with their owners. rawgl's source files carry Gregory Montoir's
copyright notices; the original source this port started from is the rawgl repository on GitHub.

## What is in this source

| Path | Contents |
|---|---|
| `rawgl-master/` | rawgl's source. Only 7 files are changed, all inside `#ifdef RAWGL32X` blocks; `RAWGL32X_PORT.patch` shows every change against the original. |
| `port/` | the 32X port: renderer, sound mixer and music player, pad mapping, platform layer |
| `port/mars/` | SH2 start-up code, linker script, Makefile |
| `cart/` | the 68000 cartridge program (from the Kobo / Tyrian 32X ports) |
| `tools/` | `aw_pack.cpp` (game data → ROM data pack), `make_cart.py` (ROM builder), `mars_boot.bin` (32X boot block) |
| `host/` | PC builds of the same engine code and the tests |
| `build.sh` | builds the complete ROM |
| `RAWGL32X_HANDOFF.md` | developer notes: decisions, measurements, test history |

## Building

Requirements:

- WSL Ubuntu (or Linux) with the Sega toolchain in `/opt/toolchains/sega`: `sh-elf` GCC 12.1 (with `g++`)
  and `m68k-elf`; plus `g++`, `zlib1g-dev`, `python3` and `python3-pil`.
- The D32XR v3.3 sources, built once, next to this folder: `../d32xr-v33/src-md`
  (the 68000 program uses its Sega CD code and `cd/cd.bin`). They are not part of this source.
- The DOS full version's data: `MEMLIST.BIN` and `BANK01` … `BANK0D` (13 bank files).

```bash
# put the game files into data/ (MEMLIST.BIN, BANK01 ... BANK0D)
./build.sh 2>&1 | tee build_log.txt
```

The ROM is written as `ANOTHERWORLD32X.32x`. In the log, `0 left out` and `password screen: yes` confirm
the full game was found. The DOS demo also builds, but contains only the first part of the game.
Do not mix demo and full-version files in one folder.

| Command | Result |
|---|---|
| `./build.sh` | the game, starting with the intro |
| `./build.sh other/folder` | game data from another folder |
| `START=16002 ./build.sh` | starts at a game part (16002 pool, 16003 jail, 16004 city …), for testing |
| no game data in `data/` | a small test ROM ("ANOTHER WORLD 32X TEST") |

## Playing

| Button | Action |
|---|---|
| D-pad | move (Up also jumps) |
| A or B | run, kick, shoot |
| C | jump (same as Up) |
| START | pause |
| MODE (6-button pad) or A+B+C together | password screen |

On the password screen, move the box over a letter with the D-pad, pick it with A, and choose OK.
Tested codes: LDKD (the pool), HTDC (the jail), CLLD, LBKG, XDDJ, FXLC, KRFK, KLFB, TTCT, DDRX
(the city, at later and later checkpoints).

## How the port works

- **The engine is rawgl, unchanged in what it does.** The game's own bytecode runs in rawgl's virtual
  machine exactly as on the PC. The port only replaces what rawgl gets from SDL and the PC: file
  loading, drawing, sound output, input and timing.
- **Game data in ROM.** `aw_pack` reads the original files with rawgl's own loader and unpacker and
  stores every resource unpacked (1.69 MB) in the ROM. The engine uses the data in place; nothing is
  copied into the 32X's 256 KB of RAM.
- **Screen.** The game draws into four 320×200 pages at 4 bits per pixel, like the original (125 KB of RAM).
  The renderer follows rawgl's original-mode drawing rules pixel for pixel; each shown frame is expanded
  into the 32X frame buffer with the game's 16-colour palette.
- **Sound.** The 32X's second SH2 mixes the game's four sound channels and runs the game's music player
  (rawgl's player translated to C) at 22,050 Hz, output through the 32X PWM. The music's timing signals
  are passed back to the game, which uses them to time the intro.
- **68000.** Runs the existing cartridge program from the earlier ports (start-up and pad reading).
- **Memory and speed.** About 131 KB of RAM is used; the SH2 program runs from ROM. The game sets its
  own pace (intro about 15–16 frames per second, levels 7–12) and the 32X keeps up in Fusion.

## Testing

The port's engine code also builds on a PC, so it can be compared directly with the original rawgl:

```bash
host/selftest.sh 2>&1 | grep -E "PASS|FAIL"   # 10 PASS lines (made-up test data, no game files needed)
host/regress.sh data 2>&1 | tail -3            # ALL PASS (needs the game files in data/)
```

`regress.sh` plays the intro (200 s) and all 36 game checkpoints (40 s each, with a scripted pad) once
with the original rawgl and once with the engine exactly as built for the 32X. Every compared frame and
the complete sound are identical.

## Known limits

- Tested in **Fusion 3.64** only; ares and real hardware have not been tried.
- Mono sound (the music's left and right channels are mixed together).
- English texts; the copy-protection screen is skipped. No saving beyond the game's own passwords.
- Only the DOS data is tested (rawgl's Amiga and Atari formats are accepted but untried); the
  15th/20th anniversary and 3DO versions are not supported.
