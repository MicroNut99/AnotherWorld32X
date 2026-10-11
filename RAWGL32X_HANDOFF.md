# RAWGL32X – Another World on Sega 32X / CD32X – handoff

Project folder: `S:\KoboPort\sega-toolchain-12.1\sega\rawgl` (WSL `/mnt/s/KoboPort/sega-toolchain-12.1/sega/rawgl`).
Source: Gregory Montoir's rawgl (`rawgl-master/`). Game: Eric Chahi. Framework: Tyrian 32X T3i (Kobo 68000
program, D32XR Sega CD code). Toolchain: `sh-elf-g++` 12.1 present (checked R1).

## Decisions
- **Cartridge only** (Micronut99, R4): a normal 32X cartridge ROM for Fusion and ares. No RAM-cart disc.
- Game data used **in place from ROM** (data pack, `tools/aw_pack.cpp`); SDRAM holds pages, engine state, heap.
- Screen pages at **4 bpp** (as the original), 125 KB. Original 1991 look only.
- C++ kept: `-fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit`, linked with gcc (no
  libstdc++); `port/cxx_support.cpp` has new/delete, pure-virtual trap, static constructors.

## Status after R6 (final for Fusion; R2-R5 results included)

| What | State |
|---|---|
| Engine changes (`#ifdef RAWGL32X`, PORT32X comments): resource, video, graphics.h, file, util | done; `RAWGL32X_PORT.patch` applies to clean rawgl |
| Data pack tool `aw_pack` (rawgl's own reader + unpacker; DOS, Amiga, Atari) | tested on synthetic data only |
| 4-bpp renderer `port/graphics_4bpp.cpp` | PC: 100 frames identical to rawgl's renderer (all drawing paths) |
| Slave mixer `port/aw_mixer.c` + `port/mixer_32x.cpp` | PC: WAV identical to rawgl's mixer (sound effects) |
| Music (R3) | the module player (rawgl's SfxPlayer ported to C) runs on the slave at sample precision; master sends start/stop/tick-delay, slave sends sync events back (VAR_MUSIC_SYNC). PC: 300 frames + stereo WAV identical to rawgl (effects, music, sync, delay change, restart). **Not yet heard on the 32X** |
| SH2 build, `build.sh`, `make_cart.py` | builds on Micronut99's PC (R2 fixes: linker symbol names, `_stat` in syscalls.c, `pri_start` global) |
| 32X, Fusion, test data | boots; picture matches the PC frames; steady 30 fps (2 refreshes per frame; the test asks 50); sound effects play (beep length 0.195 s = PC) |
| 32X, Fusion, real game (DOS data, on Micronut99's PC only) | intro plays, sound effects play; controls not tested yet (the intro takes no input) |
| `START=16002 ./build.sh` (R2c) | ROM starts at that part (16002 = water level) |
| Controls (R3b) | Fusion: walk, run, kick, jump all work (C = Up: DOS/Amiga rawgl ignores `_pi.jump`) |
| Music (R3) on the 32X | Micronut99: "I think it does play" - not measured |
| R6 | README.md written. Fusion (Micronut99): password codes LDKD (pool), HTDC (jail), CLLD … DDRX (city checkpoints) all work - the same codes were checked on the PC through the 32X pad mapping. Open: ares, real hardware, play-through notes |
| R5 | **real game data (DOS full version) tested on the PC**: `host/regress.sh data` - intro 200 s + all 36 checkpoints 40 s each with a scripted pad: reference rawgl and the 32X engine identical (frames + audio), ASan clean; same ALL PASS on Micronut99's PC. Pack 1.69 MB, 0 left out, password screen present. Game pacing: intro ~15-16 fps, levels 7-12 fps, password screen up to 25 |
| R4c | the DOS password screen has its own letter grid (D-pad + action): the pad mapping is the same there as everywhere; the R4 letter chooser is gone |
| R4b | Micronut99: no button opened the password screen. Added: A+B+C together as well as MODE; a 2-5 s border notice when the request arrives ("PASSWORD SCREEN..." or "NO PASSWORD SCREEN IN THIS GAME DATA"); aw_pack prints "password screen: yes/no" |
| R4 | shared pad mapping `port/pad_map.cpp` (32X and PC harness, unit test); password screen by pad: Up/Down letter, Right type, Left delete, A/B confirm, help line in the bottom border (`plat_set_status`); credits screen at power-on (5 s or START/A/B/C, `plat_text_pause`). **Untested on the 32X**; the password screen needs the real data (16008) |
| Real game data | used on Micronut99's PC; **not uploaded here** - no PC reference run of the real game yet |

SDRAM (R2 build): 6,080 bytes copied at boot, 236,016 bytes for malloc; pages 125 KB + engine ~6 KB.
Slave output: stereo mix as rawgl, PWM gets (L+R)/2 (mono register, Kobo's proven set-up).

Engine object: 195 KB → 7.5 KB (x86-64 measure) – rawgl's 192 KB bitmap conversion buffers are gone under RAWGL32X.

## Measurements (R1, from rawgl's Amiga table – confirm with aw_pack on the real data)
1.43 MB packed / 1.71 MB unpacked; sounds 700 KB, polygons 367 KB, bitmaps 384 KB, bytecode 185 KB.
Fixed load per part up to 149 KB (city). SH2 code (R1 test compile): 64 KB, runs from ROM.

## Layout
| Path | Role |
|---|---|
| `rawgl-master/` | rawgl, changed only under `RAWGL32X` (see the patch) |
| `port/` | 32X port: renderer, mixers, SystemStub, main, C++ support, `plat_mars.c`/`plat.h` (from Tyrian) |
| `port/mars/` | SH2 Makefile, `rawgl.ld` (Tyrian's + .init_array), `crt0.s` (Tyrian's, unchanged), syscalls |
| `cart/` | 68000 program from Tyrian T3i, unchanged (needs `../d32xr-v33/src-md`) |
| `tools/` | `aw_pack.cpp`, `make_cart.py`, `mars_boot.bin` |
| `host/` | PC builds: `rawgl_host` (reference), `rawgl_host32x` (the 32X engine), `aw_pack`; `selftest.sh` |
| `build.sh` | full ROM build → `ANOTHERWORLD32X.32x` (replaces the Kobo CD32X build.sh; `sh2/`, `subcpu/` unused) |

## Build and test
- `./build.sh [data folder] 2>&1 | tee build_log.txt` – default folder `./data`; without data: self-test
  data, ROM title `ANOTHER WORLD 32X TEST`.
- `host/selftest.sh` – PASS lines (reference repeatable; 32X engine frames + audio identical; ASan).
- Compare a real run: `aw_pack DATA x.pack`, then the same `AW_PAD` script on `rawgl_host --datapath=DATA`
  and `AW_PACK=x.pack rawgl_host32x`, then `compare_frames.py`.

Host variables: `AW_PAD` (`t5000:A,t5200:-`; L R U D A B C S), `AW_MAX_SECONDS`, `AW_MAX_FRAMES`,
`AW_FRAMES_DIR`, `AW_SAVE_EVERY`, `AW_AUDIO_WAV`, `AW_LOG`, `AW_PACK`.

## 32X details
- SDRAM: .sdcode/.data/.bss, heap (malloc: pages 125 KB, Engine ~7 KB), stacks 16 KB + 2 KB (Tyrian layout).
- Display: `plat_present_4bpp` expands the page through a 256-entry table, 16,000 32-bit frame buffer writes;
  palette: 16 CRAM entries. Frame pacing off (`T32X_NO_PACING`): the game paces itself (pause slices × 20 ms).
- Sound: MixCmd ring to the slave (Tyrian's), PWM 22,050 Hz mono; sound data read from ROM by the slave.
- Pad: A/B run-kick-shoot, C or Up jump, START pause, MODE or A+B+C password screen (port/pad_map.h).
- Errors: rawgl's `error()` → red screen with the message; `warning()` → "LAST LOG" line.

## Next
1. ares boot; play-through notes (slow / wrong scenes), if any.
2. Real data uploaded here → aw_pack report, regression runs (reference vs 32X engine) through the game.
3. Speed (profiler panel: Tyrian's needs AW slots before PROFILE=1 builds), if any scene is slow.
