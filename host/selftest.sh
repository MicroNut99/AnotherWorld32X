#!/bin/bash
# RAWGL32X harness self-test (PC).  Synthetic data (make_test_data.py) ->
#   1. reference build (rawgl_host: unchanged engine, rawgl's renderer + mixer), run twice
#   2. aw_pack -> data pack -> the 32X engine build (rawgl_host32x: pack used in place,
#      read-only; 4-bpp renderer; the slave SH2's mixer)
#   3. frames and audio of 1 and 2 must be identical (300 frames: effects, music, sync events,
#      tick-delay change, music restart); triangle position, music, sync, pad log checked
#   4. all of it again under AddressSanitizer
# Prints PASS / FAIL lines.
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
python3 make_test_data.py "$T/data" >/dev/null
run() {  # $1 = binary, $2 = output folder
	mkdir -p "$2"
	AW_PACK="$T/aw.pack" AW_MAX_FRAMES=300 AW_FRAMES_DIR="$2" AW_SAVE_EVERY=1 AW_AUDIO_WAV="$2/audio.wav" \
	AW_PAD="t500:R,t700:-" AW_LOG="$2/log.txt" "$1" --datapath="$T/data" >/dev/null 2>"$2/stderr.txt"
}
checks() {  # $1 = label prefix, $2 = suffix of the folders
	run ./rawgl_host "$T/a$2"; run ./rawgl_host "$T/b$2"
	./aw_pack "$T/data" "$T/aw.pack" >/dev/null
	run ./rawgl_host32x "$T/c$2"
	python3 compare_frames.py "$T/a$2" "$T/b$2" >/dev/null && echo "PASS $1reference: repeat run draws the same frames" || echo "FAIL $1reference: repeat run differs"
	cmp -s "$T/a$2/audio.wav" "$T/b$2/audio.wav" && echo "PASS $1reference: repeat run, same audio" || echo "FAIL $1reference: audio differs"
	python3 compare_frames.py "$T/a$2" "$T/c$2" >"$T/cmp.txt" && echo "PASS $132X engine: all 300 frames identical to the reference" || { echo "FAIL $132X engine frames:"; cat "$T/cmp.txt"; }
	cmp -s "$T/a$2/audio.wav" "$T/c$2/audio.wav" && echo "PASS $132X engine: audio identical to the reference" || echo "FAIL $132X engine audio differs"
	for d in a b c; do
		if grep -q "ERROR: AddressSanitizer\|ERROR:" "$T/$d$2/stderr.txt"; then echo "FAIL $1run $d:"; head -20 "$T/$d$2/stderr.txt"; fi
	done
}
make -s all
./test_padmap || true
checks "" ""
python3 - "$T/a" <<'PY' && echo "PASS frames, sound, music sync and pad log as expected" || echo "FAIL frames/sound/pad log"
import sys, wave, struct
from PIL import Image
d = sys.argv[1]
for n in (0, 10, 50, 90):
    px = Image.open("%s/f%06d.png" % (d, n)).convert("RGB").load()
    xs = [x for y in range(200) for x in range(320) if px[x, y] == (255, 255, 255)]
    want = (32 + 2 * n) & 255
    assert abs((min(xs) + max(xs)) / 2 - want) <= 1, (n, min(xs), max(xs), want)
w = wave.open(d + "/audio.wav"); s = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))
assert w.getnframes() == 300 * 441                       # 6 s of audio
assert s[0] != 0                                          # the beep starts at once
seg = s[int(1.0 * 22050) * 2:int(1.1 * 22050) * 2]
assert max(seg[0::2]) > 1000 and max(seg[1::2]) > 1000    # music on both sides at 1 s
assert seg[0::2] != seg[1::2]                             # and not the same on both
# R3: the music's sync event shows the extra rectangle from frame 11 to 40 (rows 10..40)
orange = Image.open("%s/f%06d.png" % (d, 0)).convert("RGB").getpixel((200, 110))
on = [i for i in range(300) if Image.open("%s/f%06d.png" % (d, i)).convert("RGB").getpixel((30, 40)) == orange]
assert on[0] == 11 and 40 in on and 41 not in on, on[:40]
log = open(d + "/log.txt").read()
assert "pad R at 500 ms" in log and "pad - at 700 ms" in log
PY
make -s asan >/dev/null 2>&1
checks "ASan: " "_asan"
make -s clean; make -s all
rm -rf "$T"
