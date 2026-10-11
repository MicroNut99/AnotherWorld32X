#!/bin/bash
# RAWGL32X real-game regression (PC): the original rawgl (reference) against the engine as
# built for the 32X, on the real game data, for the intro (200 s) and all 36 checkpoints
# (40 s each, a scripted pad).  Every 4th frame and the whole audio must be identical.
#   host/regress.sh DATA_DIR        (e.g. host/regress.sh data)
set -e
DATA="$(cd "${1:-$(dirname "$0")/../data}" && pwd)"   # resolved before the cd below
cd "$(dirname "$0")"
T=$(mktemp -d)
make -s all
./aw_pack "$DATA" "$T/aw.pack" | head -3
PAD="t1000:R,t3000:RA,t4000:-,t5000:L,t6000:A,t6500:-,t7000:U,t7300:-,t8000:D,t9000:-,t10000:RU,t10400:-,t12000:A,t14000:-,t15000:L,t17000:R,t19000:-,t20000:A,t20200:-,t22000:RA,t24000:-"
fail=0
run() {  # $1 = part, $2 = seconds
	rm -rf "$T/r" "$T/x"; mkdir -p "$T/r" "$T/x"
	AW_PAD="$PAD" AW_MAX_SECONDS=$2 AW_FRAMES_DIR="$T/r" AW_SAVE_EVERY=4 AW_AUDIO_WAV="$T/r/a.wav" \
		./rawgl_host --datapath="$DATA" --part=$1 >/dev/null 2>"$T/r/err.txt"
	AW_PACK="$T/aw.pack" AW_PAD="$PAD" AW_MAX_SECONDS=$2 AW_FRAMES_DIR="$T/x" AW_SAVE_EVERY=4 AW_AUDIO_WAV="$T/x/a.wav" \
		./rawgl_host32x --part=$1 >/dev/null 2>"$T/x/err.txt"
	local f a
	f=$(python3 compare_frames.py "$T/r" "$T/x" | head -1)
	a=$(cmp -s "$T/r/a.wav" "$T/x/a.wav" && echo "audio same" || echo "AUDIO DIFFERENT")
	if [[ "$f" == *" 0 different, 0 missing, 0 extra"* && "$a" == "audio same" ]] && ! grep -q "ERROR" "$T/x/err.txt"; then
		echo "PASS part $1: $f, $a"
	else
		echo "FAIL part $1: $f, $a"; fail=1
	fi
}
run 16001 200
for p in $(seq 0 35); do run $p 40; done
rm -rf "$T"
[ $fail = 0 ] && echo "ALL PASS" || echo "*** SOME FAILED"
