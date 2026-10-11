#!/bin/bash
# Another World 32X (RAWGL32X) - cartridge ROM build.
#   ./build.sh [data folder]  -> ANOTHERWORLD32X.32x (4 MB). Test in Fusion / ares as a 32X cartridge.
# Without a data folder (and no ./data): the self-test data (a moving triangle, a bitmap, text,
# one beep - host/make_test_data.py) -> the ROM title says TEST.
#
# Pieces:
#   host/ aw_pack          original data files -> build/aw.pack (unpacked, used in place in ROM)
#   port/mars/Makefile     rawgl engine + port -> port/mars/rawgl_sh2.elf/.bin (sh-elf g++/gcc)
#   cart/cart_md.s         Kobo's 68000 program (via Tyrian T3i, unchanged) + D32XR's Sega CD code
#   tools/make_cart.py     everything -> ANOTHERWORLD32X.32x
# (This replaces the Kobo CD32X build.sh that came with the folder; sh2/ and subcpu/ are not used.)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROM="$HERE/ANOTHERWORLD32X.32x"
rm -f "$ROM"
trap 'echo; echo "******************************************************************"; \
      echo "*** BUILD FAILED - NO ROM WAS WRITTEN."; \
      echo "*** build.sh line $LINENO: $BASH_COMMAND"; \
      echo "*** Please send: grep -n -B6 \"rror\" build_log.txt"; \
      echo "******************************************************************"' ERR
TC68=/opt/toolchains/sega/m68k-elf/bin/m68k-elf-

DATA="${1:-$HERE/data}"
mkdir -p "$HERE/build"

echo "==> PC tools (host/aw_pack)"
make -s -C "$HERE/host" aw_pack

SUFFIX=""
FOUND=""
for f in memlist.bin MEMLIST.BIN Memlist.bin bank01 BANK01 Bank01; do
  [ -f "$DATA/$f" ] && FOUND=1
done
if [ -n "$FOUND" ]; then
  echo "==> data pack from $DATA"
else
  echo "==> no game data in $DATA: using the self-test data (ROM title: TEST)"
  python3 "$HERE/host/make_test_data.py" "$HERE/build/testdata" >/dev/null
  DATA="$HERE/build/testdata"
  SUFFIX="TEST"
fi
"$HERE/host/aw_pack" "$DATA" "$HERE/build/aw.pack"

echo "==> SH2 (rawgl engine, code runs from ROM)"
# rebuilt from scratch whenever a source changed (checksum, not file times: unzip on a
# Windows drive can leave old objects looking newer than changed sources)
STAMP_FILE="$HERE/port/mars/obj/.source_stamp"
STAMP="$( (cd "$HERE" && cat rawgl-master/*.cpp rawgl-master/*.h port/*.c port/*.cpp port/*.h \
            port/mars/*.c port/mars/*.h port/mars/*.s port/mars/*.ld port/mars/Makefile; echo "START=${START:-}") | md5sum | cut -c1-12)"
if [ "$(cat "$STAMP_FILE" 2>/dev/null)" != "$STAMP" ]; then
  echo "    sources changed since the last SH2 build: rebuilding everything"
  make -C "$HERE/port/mars" clean >/dev/null
fi
# R2c: START=16002 ./build.sh -> the ROM starts at that game part (16001 = intro, the default)
make -C "$HERE/port/mars" -j"${JOBS:-1}" EXTRA_CFLAGS="${START:+-DRAWGL32X_START_PART=$START}"
mkdir -p "$HERE/port/mars/obj" && echo "$STAMP" > "$STAMP_FILE"

echo "==> 68000 (cart/cart_md.s from Kobo via Tyrian, unchanged)"
D32="$HERE/../d32xr-v33/src-md"
[ -f "$D32/cd/cd.bin" ] || { echo "*** $D32/cd/cd.bin missing - build d32xr-v33 once (make) first"; false; }
cd "$HERE/cart"
cp "$D32/cd/cd.bin" cd.bin
${TC68}as -m68000 --register-prefix-optional cart_md.s -o cart_md.o
${TC68}as -m68000 --register-prefix-optional net_link.s -o net_link.o
${TC68}as -m68000 --register-prefix-optional "$D32/kos.s" -o kos.o
${TC68}gcc -m68000 -Os -c -fomit-frame-pointer -fno-builtin-printf "$D32/scd.c" -o scd.o
${TC68}gcc -m68000 -Os -c -fomit-frame-pointer -std=gnu99 cd_files.c -o cd_files.o
${TC68}gcc -m68000 -Os -c -fomit-frame-pointer -std=gnu99 mouse.c -o mouse.o
${TC68}gcc -T cart.ld -nostdlib -Wl,-Map=cart.map cart_md.o scd.o kos.o cd_files.o mouse.o net_link.o -lc -lgcc -o cart_md.elf
${TC68}objcopy -O binary -j .text -j .data cart_md.elf cart_md.bin

echo "==> ROM"
python3 "$HERE/tools/make_cart.py" "$HERE/tools/mars_boot.bin" \
  "$HERE/port/mars/rawgl_sh2.elf" "$HERE/port/mars/rawgl_sh2.bin" \
  "$HERE/cart/cart_md.bin" "$HERE/build/aw.pack" "$ROM" $SUFFIX
echo "==> done: $ROM"
echo "    Crash/exception PCs: look them up in port/mars/rawgl.map"
