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

### What you need

- WSL Ubuntu (or Linux) with the Sega toolchains in `/opt/toolchains/sega`: `sh-elf` GCC 12.1 (with
  `g++`) and `m68k-elf` (`as`, `gcc`, `objcopy`). `build.sh` uses that folder for the 68000 tools;
  the SH2 part also accepts `GENDEV` (below).
- `g++`, `make`, `zlib1g-dev`, `python3` and `python3-pil` (Pillow).
- The D32XR v3.3 sources, built once (`make`), next to this folder: `../d32xr-v33/src-md`. The 68000
  program uses its Sega CD code (`scd.c`, `kos.s`) and `cd/cd.bin`. They are not part of this source.
- The DOS full version's data: `MEMLIST.BIN` and `BANK01` … `BANK0D` (13 bank files; upper or lower
  case).

### Steps

```bash
# 1. put the game files into data/ (MEMLIST.BIN, BANK01 ... BANK0D)
# 2. build
./build.sh 2>&1 | tee build_log.txt
```

The ROM is written as `ANOTHERWORLD32X.32x` (4 MB). In the log, `0 left out` and `password screen: yes`
confirm the full game was found. The DOS demo also builds, but contains only the first part of the
game. Do not mix demo and full-version files in one folder. If a step fails, no ROM is written and the
log says which line of `build.sh` stopped.

### build.sh: arguments and settings

`./build.sh [data folder]`

| Setting | Effect |
|---|---|
| `./build.sh` | the game from `data/`, starting with the intro |
| `./build.sh other/folder` | the game data from another folder |
| no game data found | a small test ROM (a moving triangle, a bitmap, text, one beep - `host/make_test_data.py`); the ROM title says `ANOTHER WORLD 32X TEST` |
| `START=n ./build.sh` | the ROM starts at game part `n` instead of the intro, for testing: 16001 intro (default), 16002 the pool, 16003 the jail, 16004 the city, 16005 the arena, 16006 the baths, 16007 the final part, 16008 the password screen; 0-35 a checkpoint (rawgl's numbering) |
| `JOBS=n ./build.sh` | the SH2 part compiled with `n` parallel jobs (default 1) |

`build.sh` rebuilds the whole SH2 program whenever any source (or `START`) changed - it keeps a checksum
of the sources rather than trusting file times.

### What build.sh does (to run the steps by hand)

1. The PC tool and the data pack (all of the game's resources, unpacked, used in place in ROM):
   ```bash
   make -C host aw_pack
   host/aw_pack data build/aw.pack                  # aw_pack DATA_DIR OUT.pack
   ```
2. The SH2 program (rawgl's engine + the port, `port/mars/Makefile`):
   ```bash
   make -C port/mars clean
   make -C port/mars -j4 [EXTRA_CFLAGS=-DRAWGL32X_START_PART=16002]
   ```
   -> `port/mars/rawgl_sh2.elf`, `rawgl_sh2.bin` and `rawgl.map` (look crash addresses up there).
   Make variables:
   - `GENDEV` (environment): the toolchain folder, default `/opt/toolchains/sega`.
   - `EXTRA_CFLAGS`: extra compiler switches, e.g. `-DRAWGL32X_START_PART=n` (what `START` sets).
   - `BUILD_ID`: defines `T32X_BUILD_ID`. A leftover from the Tyrian port; nothing in this game shows it.

   Make does not see a change of `EXTRA_CFLAGS`, so run `make clean` first.

   Switches the Makefile always sets: `RAWGL32X` (the port's changes in rawgl's code), `RAWGL32X_MARS`
   (the 32X side), `BYPASS_PROTECTION` (no copy-protection screen), `NDEBUG`, `__32X__` / `MARS`
   (the platform code), `T32X_NO_PACING` (the game sets its own frame rate; without it the Tyrian
   port's frame pacing would run).

   Switches you can add with `EXTRA_CFLAGS`:
   - `-DRAWGL32X_START_PART=n`: the start part (see `START`).
   - `-DT32X_PROFILE`: the Tyrian port's frame profiler panel. Only its presentation, overlay and
     flip times are measured here; untried in this game.
3. The 68000 program (`cart/`, with D32XR's Sega CD code):
   ```bash
   cd cart
   cp ../../d32xr-v33/src-md/cd/cd.bin cd.bin
   m68k-elf-as -m68000 --register-prefix-optional cart_md.s -o cart_md.o
   m68k-elf-as -m68000 --register-prefix-optional net_link.s -o net_link.o
   m68k-elf-as -m68000 --register-prefix-optional ../../d32xr-v33/src-md/kos.s -o kos.o
   m68k-elf-gcc -m68000 -Os -c -fomit-frame-pointer -fno-builtin-printf ../../d32xr-v33/src-md/scd.c -o scd.o
   m68k-elf-gcc -m68000 -Os -c -fomit-frame-pointer -std=gnu99 cd_files.c -o cd_files.o
   m68k-elf-gcc -m68000 -Os -c -fomit-frame-pointer -std=gnu99 mouse.c -o mouse.o
   m68k-elf-gcc -T cart.ld -nostdlib -Wl,-Map=cart.map cart_md.o scd.o kos.o cd_files.o mouse.o net_link.o -lc -lgcc -o cart_md.elf
   m68k-elf-objcopy -O binary -j .text -j .data cart_md.elf cart_md.bin
   cd ..
   ```
4. The ROM:
   ```bash
   python3 tools/make_cart.py tools/mars_boot.bin port/mars/rawgl_sh2.elf port/mars/rawgl_sh2.bin \
       cart/cart_md.bin build/aw.pack ANOTHERWORLD32X.32x [TEST]
   ```
   The optional last word is added to the ROM's title (`TEST` for the test data).

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

`regress.sh DATA_DIR` plays the intro (200 s) and all 36 game checkpoints (40 s each, with a scripted pad)
once with the original rawgl and once with the engine exactly as built for the 32X. Every compared frame
and the complete sound are identical. `selftest.sh` does the same on made-up data, then again under
AddressSanitizer.

### The PC builds (`host/`)

`make -C host [target]` (no SDL needed):

| Target | What |
|---|---|
| `all` (default) | the four below |
| `rawgl_host` | the reference: rawgl's engine unchanged, the original data files, rawgl's renderer and mixer |
| `rawgl_host32x` | the engine as built for the 32X (`-DRAWGL32X`): the data pack used in place, the 4-bpp renderer, the 32X mixer |
| `aw_pack` | original data files -> data pack (and a size report) |
| `test_padmap` | the pad-mapping unit test |
| `asan` | all of them again with AddressSanitizer |
| `clean` | removes the builds |

Make variables: `CXX`, `CC` (the compilers), `OPT` (optimisation, default `-O2`) and `SAN` (sanitizer
flags; `asan` sets them).

`rawgl_host` / `rawgl_host32x` arguments: `--datapath=DIR` (the game files; `rawgl_host32x` reads the
pack named by `AW_PACK` instead), `--part=NUM` (start part, as `START` above), `--language=us|fr`.

Environment variables:

| Variable | Effect |
|---|---|
| `AW_PACK=file` | `rawgl_host32x`: the data pack to use |
| `AW_MAX_FRAMES=n` | stop after `n` frames |
| `AW_MAX_SECONDS=n` | stop after `n` seconds of game time |
| `AW_FRAMES_DIR=dir` | save the frames there as PNG |
| `AW_SAVE_EVERY=n` | save every `n`-th frame only (default 1) |
| `AW_PAD=script` | a scripted pad: `tMS:BUTTONS,...` - from `MS` milliseconds on, the buttons `L R U D A B C S M` (Start = `S`, Mode = `M`), `-` for none; e.g. `t1000:R,t3000:RA,t4000:-` |
| `AW_AUDIO_WAV=file` | write the whole sound output as WAV |
| `AW_LOG=file` | a copy of the run's report: the data type, when each part starts, the memory each part used, the sound written |

Other tools: `host/compare_frames.py REF_DIR NEW_DIR` (two folders of frame PNGs: the ones that differ),
`host/make_test_data.py [folder]` (the made-up test data, default `testdata`).

rawgl's own PC version (SDL2 and OpenGL, `rawgl-master/Makefile`) is unchanged by the port. Its
build is described in [rawgl-master/README.md](rawgl-master/README.md); it has not been tried here.

## Known limits

- Tested in **Fusion 3.64** only; ares and real hardware have not been tried.
- Mono sound (the music's left and right channels are mixed together).
- English texts; the copy-protection screen is skipped. No saving beyond the game's own passwords.
- Only the DOS data is tested (rawgl's Amiga and Atari formats are accepted but untried); the
  15th/20th anniversary and 3DO versions are not supported.
