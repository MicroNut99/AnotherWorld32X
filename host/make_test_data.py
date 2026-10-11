#!/usr/bin/env python3
"""
RAWGL32X harness self-test data - NOT game data.

Writes a tiny DOS-format data set (memlist.bin + bank01) that the unchanged rawgl engine
loads like the real game.  Part 16001 gets a palette, a short bytecode program, shapes, a
bitmap and one sound, chosen to exercise every drawing path the 32X renderer has:

  - a planar bitmap loaded into page 0 (diagonal stripes, so colour edges fall on odd x)
  - page 0 copied to page 1 with a vertical scroll that changes every frame
  - a plain rectangle, a "copy from page 0" polygon, a "transparent" (bit 3) polygon
  - a group shape: a recoloured child and a sprite mask (sprite 3)
  - a zoomed triangle moving right: x = var 0x10, +2 per frame, wraps at 256 (pure white,
    colour 3; no other pixel is white, so the self-test can find it)
  - a text line (string 0x001) in colour 15
  - R3 music: a 2-pattern module (resource 3) with two instruments (a looped sine,
    resource 2, and the beep), volume up/down effects, a note-off, a very high note, and
    sync events: the script draws an extra rectangle while VAR_MUSIC_SYNC == 0x42;
    at frame 48 the script changes the tick delay; at every wrap the music restarts
  - one sound at the start and each time the triangle wraps round (every 128 frames):
    2048 samples at period 20 (~0.2 s square wave)

    python3 make_test_data.py OUTDIR
"""
import os, struct, sys

out = sys.argv[1] if len(sys.argv) > 1 else "testdata"
os.makedirs(out, exist_ok=True)

# ---- palette 0 (Amiga 0x0RGB), colour 3 = the only pure white -------------------------------
pal = [0x000, 0x115, 0xF80, 0xFFF, 0x0A0, 0xA0A, 0x0AA, 0x555,
       0x800, 0x080, 0x008, 0x880, 0x808, 0x088, 0xCC0, 0xEEE]
palette = b"".join(struct.pack(">H", c) for c in pal) + b"\0" * (1024 - 32)
palette += palette  # EGA area

# ---- shapes (segVideo1) ----------------------------------------------------------------------
def poly(colour_byte, w, h, pts):
    b = bytes([colour_byte, w, h, len(pts)]) + bytes(v for p in pts for v in p)
    return b + b"\0" * (16 - len(b))
shapes  = poly(0xC0 | 2, 80, 60, [(80, 0), (80, 60), (0, 60), (0, 0)])       # @0  rectangle
shapes += poly(0xC0 | 3, 20, 40, [(10, 0), (20, 40), (0, 40), (10, 0)])      # @16 triangle
shapes += poly(0xC0 | 0x11, 100, 50, [(100, 0), (100, 50), (0, 50), (0, 0)]) # @32 copy from page 0
shapes += poly(0xC0 | 0x10, 61, 80, [(61, 0), (61, 80), (0, 80), (0, 0)])    # @48 transparent
group = bytes([0x02, 0, 0, 1,                         # @64 group: dx, dy, n = 2 children - 1
               0x80, 0x08, 10, 10, 0x05, 0x00,        # child: triangle (word 8), colour 5
               0x80, 0x00, 40, 10, 0x8E, 0x03])       # child: sprite mask 3, colour 14
shapes += group

# ---- bitmap 320x200, 4 planes of 8000 bytes (DOS/Amiga layout) --------------------------------
def colour(x, y):
    c = ((x + y) // 7) & 15
    return 4 if c == 3 else c            # keep pure white for the triangle only
planes = [bytearray(8000) for _ in range(4)]
for y in range(200):
    for xb in range(40):
        for b in range(8):
            c = colour(xb * 8 + b, y)
            for p in range(4):
                if c & (1 << p):
                    planes[p][y * 40 + xb] |= 0x80 >> b
bitmap = b"".join(bytes(p) for p in planes)

# ---- bytecode (segCode) ----------------------------------------------------------------------
code = bytes([
    0x0B, 0x00, 0x00,                    # setPalette 0
    0x19, 0x00, 0x01,                    # updateResources 1 (sound)
    0x19, 0x00, 0x12,                    # updateResources 0x12 (bitmap -> page 0)
    0x00, 0x10, 0x00, 0x20,              # var10 = 32
    0x00, 0xFF, 0x00, 0x01,              # VAR_PAUSE_SLICES = 1 (one 50 Hz tick per frame)
    0x00, 0xF9, 0x00, 0x00,              # VAR_SCROLL_Y = 0
    0x18, 0x00, 0x01, 0x14, 0x3F, 0x00,  # playSound res 1, period 20, vol 63, channel 0
    0x19, 0x00, 0x02,                    # updateResources 2 (instrument: looped sine)
    0x19, 0x00, 0x03,                    # updateResources 3 (music module)
    0x1A, 0x00, 0x03, 0x00, 0x00, 0x00,  # playMusic res 3, module's own delay, order 0
])
L = len(code)
code += bytes([
    0x0D, 0x01,                          # selectPage 1
    0x0F, 0x80, 0x01,                    # copyPage 0 -> 1 with VAR_SCROLL_Y
    0x80, 0x00, 0xA0, 0x64,              # rectangle at 160,100
    0x80, 0x10, 0x50, 0x64,              # copy-from-page-0 polygon (word 16) at 80,100
    0x80, 0x18, 0xF0, 0x50,              # transparent polygon (word 24) at 240,80
    0x80, 0x20, 0x40, 0xA0,              # group (word 32) at 64,160
])
# R3: the extra rectangle while the music's sync variable is 0x42
skipr = len(code) + 6 + 4
code += bytes([
    0x0A, 0x01, 0xF4, 0x42, skipr >> 8, skipr & 0xFF,  # if VAR_MUSIC_SYNC != 0x42: skip
    0x80, 0x00, 0x30, 0x30,              # rectangle at 48,48
])
assert len(code) == skipr
code += bytes([
    0x5A, 0x00, 0x08, 0x10, 0x32, 0x60,  # triangle, x = var10, y = 50, zoom 0x60
    0x12, 0x00, 0x01, 0x02, 0xB4, 0x0F,  # drawString 0x001 at column 2, y 180, colour 15
    0x03, 0x10, 0x00, 0x02,              # var10 += 2
    0x14, 0x10, 0x00, 0xFF,              # var10 &= 255
    0x03, 0xF9, 0x00, 0x01,              # scroll += 1
    0x14, 0xF9, 0x00, 0x1F,              # scroll &= 31
])
# R2b: the beep again every time the triangle wraps (var10 back to 0, every 128 frames)
# R3: ... and the music restarts
skip = len(code) + 6 + 6 + 6
code += bytes([
    0x0A, 0x01, 0x10, 0x00, skip >> 8, skip & 0xFF,   # if var10 != 0: skip the sound
    0x18, 0x00, 0x01, 0x14, 0x3F, 0x00,  # playSound res 1, period 20, vol 63, channel 0
    0x1A, 0x00, 0x03, 0x00, 0x00, 0x00,  # playMusic res 3, module's delay, order 0
])
assert len(code) == skip
# R3: at var10 == 128 (frame 48) the tick delay changes (playMusic 0 with a delay)
skipd = len(code) + 6 + 6
code += bytes([
    0x0A, 0x01, 0x10, 0x80, skipd >> 8, skipd & 0xFF,
    0x1A, 0x00, 0x00, 0x07, 0x08, 0x00,  # playMusic 0, delay 0x0708 (1800): faster ticks
])
assert len(code) == skipd
code += bytes([
    0x10, 0x01,                          # updateDisplay page 1
    0x06,                                # yield
    0x07, 0x00, L,                       # jmp L
])

# ---- sound (type 0): BE length in words, BE loop length in words, 4 pad bytes, signed 8-bit ----
n = 2048
pcm = bytes(((40 if (i // 16) % 2 else -40) & 0xFF) for i in range(n))
sound = struct.pack(">HH", n // 2, 0) + b"\0" * 4 + pcm

# ---- R3: instrument (looped sine) and music module ----
import math
sine = bytes((int(round(90 * math.sin(2 * math.pi * i / 64))) & 0xFF) for i in range(128))
instrument = struct.pack(">HH", 32, 32) + b"\0" * 4 + sine          # 64 bytes, then a 64-byte loop
def row(notes):  # 4 channels: (note1, note2)
    return b"".join(struct.pack(">HH", a, b) for a, b in notes)
def pattern(k):
    rows = []
    for r in range(64):
        n = [(0, 0)] * 4
        if r % 4 == 0:
            n[0] = ([428, 381, 339, 320][(r // 4 + k) % 4], 0x1000)              # sine
        if r % 8 == 4:
            n[1] = (214 if k == 0 or r != 36 else 0x60, 0x2608)                   # beep, volume down 8
        if r == 2:
            n[2] = (254, 0x1510)                                                  # sine, volume up 0x10
        if r == 30:
            n[2] = (0xFFFE, 0)                                                    # note off
        if r == 10:
            n[3] = (0xFFFD, 0x0042)                                               # sync: 0x42
        if r == 40:
            n[3] = (0xFFFD, 0x0007)                                               # sync: 7
        rows.append(row(n))
    return b"".join(rows)
module = struct.pack(">H", 2400)                                  # tick delay (~20 ms)
module += struct.pack(">HH", 2, 0x30) + struct.pack(">HH", 1, 0x20) + b"\0" * (13 * 4)
module += b"\0" + bytes([2])                                      # 0x3E pad, 0x3F numOrder
module += bytes([0, 1]) + b"\0" * 126                             # 0x40 order table
module += pattern(0) + pattern(1)
assert len(module) == 0xC0 + 2048

# ---- bank01 + memlist.bin (20-byte entries, status 0xFF ends the list) ----
entries = {0x01: (0, sound), 0x02: (0, instrument), 0x03: (1, module),
           0x12: (2, bitmap), 0x17: (3, palette), 0x18: (4, code), 0x19: (5, shapes)}
bank = b""
ml = b""
for i in range(146):
    if i in entries:
        typ, data = entries[i]
        pos = len(bank)
        bank += data
        ml += struct.pack(">BBIBBIII", 0, typ, 0, 0, 1, pos, len(data), len(data))
    else:
        ml += struct.pack(">BBIBBIII", 0, 0, 0, 0, 0, 0, 0, 0)
ml += struct.pack(">BBIBBIII", 0xFF, 0, 0, 0, 0, 0, 0, 0)
open(os.path.join(out, "bank01"), "wb").write(bank)
open(os.path.join(out, "memlist.bin"), "wb").write(ml)
print("test data written to %s: memlist.bin (%d bytes), bank01 (%d bytes)" % (out, len(ml), len(bank)))
