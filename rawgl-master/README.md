**Note (Another World 32X):** this is rawgl's own README, for its PC version. In this repository the
engine is also built for the Sega 32X; its changes are all inside `#ifdef RAWGL32X` and leave this PC
version as it was. To build the 32X cartridge, its switches and the PC test builds, see
[Building](../README.md#building) in the port's README.

rawgl's PC version needs SDL2, SDL2_mixer, OpenGL, zlib and libmt32emu: `make` in this folder (`rawgl-master/Makefile`,
switches `-DBYPASS_PROTECTION` and `-DUSE_GL` set there) gives `rawgl`, run as below. It has not been
tried in this repository.


# raw(gl) - Another World Interpreter

rawgl is a re-implementation of the engine used in the game Another World.

![Screenshot Intro Amiga](docs/screenshot-intro-amiga.png) ![Screenshot Intro 3DO](docs/screenshot-intro-3do.png)

## Supported Versions

The program requires the original data files.

- Amiga (Bank*)
- Atari (Bank*)
- Atari demo (AW.TOS)
- DOS (Bank*, memlist.bin)
- DOS demo (Demo*, memlist.bin)
- 15th Anniversary Edition (Data/Pak01.pak, Menu/, Music/)
- 20th Anniversary Edition (game/)
- Windows 3.1 (Bank, *mid)
- 3DO (GameData/ or .ISO)

## Running

By default, the engine tries to load the game data files from the current
directory. This can be changed with command line switches.

```
  Usage: rawgl [OPTIONS]...
    --datapath=PATH   Path to data files (default '.')
    --language=LANG   Language (fr,us,de,es,it)
    --part=NUM        Game part to start from (0-35 or 16001-16009)
    --render=NAME     Renderer (original,software,gl)
    --window=WxH      Windowed display size (default '640x400')
    --fullscreen      Fullscreen display (stretched)
    --fullscreen-ar   Fullscreen display (16:10 aspect ratio)
    --ega-palette     Use EGA palette with DOS version
    --demo3-joy       Use inputs from 'demo3.joy' (DOS demo)
    --difficulty=DIFF Difficulty (easy,normal,hard)
    --audio=AUDIO     Audio (original,remastered)
    --mt32            Use MT32 sounds mapping with DOS version
```

In game hotkeys :

```
  Arrow Keys      move Lester
  Enter/Space     run/shoot
  C               enter a code to start at a specific position
  P               pause the game
  Alt X           exit the game
```

## Technical Details

- [Amiga/DOS](docs/Amiga_DOS.md)
- [3DO](docs/3DO.md)
- [WiiU](docs/WiiU.md)
