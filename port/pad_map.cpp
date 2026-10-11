/* RAWGL32X - pad mapping (see pad_map.h). */
#include "pad_map.h"
#include "plat.h"
#include <string.h>

void PadMapper::apply(uint16_t pad, PlayerInput &pi, bool passwordScreen) {
	const uint16_t pressed = pad & ~_prev;
	_prev = pad;
	pi.dirMask = 0;
	pi.action = (pad & (PAD_A | PAD_B)) != 0;
	pi.jump = (pad & PAD_C) != 0;
	// R4c: the DOS password screen has its own letter grid, driven by the D-pad and the
	// action button like the rest of the game - so the pad works there as everywhere.
	(void)passwordScreen;
	if (pad & PAD_LEFT)  pi.dirMask |= PlayerInput::DIR_LEFT;
	if (pad & PAD_RIGHT) pi.dirMask |= PlayerInput::DIR_RIGHT;
	if (pad & PAD_UP)    pi.dirMask |= PlayerInput::DIR_UP;
	if (pad & PAD_DOWN)  pi.dirMask |= PlayerInput::DIR_DOWN;
	// R3b: jump = Up in DOS/Amiga rawgl (it reads _pi.jump only for the 3DO version)
	if (pad & PAD_C)     pi.dirMask |= PlayerInput::DIR_UP;
	if (pressed & PAD_START) pi.pause = true;   // like a key press: the script clears it
	// R4b: MODE, or A+B+C pressed together (the moment the third one goes down)
	const uint16_t abc = PAD_A | PAD_B | PAD_C;
	if ((pressed & PAD_MODE) || ((pad & abc) == abc && (pressed & abc))) {
		pi.code = true;
		_notice = 120;   // processEvents runs about once per game frame: ~2-5 s
	}
}

const char *PadMapper::helpLine(bool passwordScreen, bool hasPasswordScreen) {
	if (!passwordScreen) {
		if (_notice == 0) return 0;
		--_notice;
		return hasPasswordScreen ? "PASSWORD SCREEN..." : "NO PASSWORD SCREEN IN THIS GAME DATA";
	}
	_notice = 0;
	return 0;   // R4c: the game shows its own letter grid
}
