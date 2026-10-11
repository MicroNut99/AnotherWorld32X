/*
 * RAWGL32X - the pad -> rawgl PlayerInput mapping, shared by the 32X (systemstub_32x.cpp)
 * and the PC harness (host/systemstub_host.cpp), so the PC tests the console's mapping.
 *
 * In the game:
 *   D-pad    move (Up = jump, as the arrow key in the original)
 *   A or B   action: run / kick / shoot
 *   C        jump (presses Up: DOS/Amiga rawgl has no separate jump input)
 *   START    pause / continue
 *   MODE     password screen (6-button pad), as the "C" key in the original
 *   A+B+C    the same, pressed together (3-button pads, or MODE not set up in the emulator)
 * On the password screen (part 16008) the D-pad types instead of moving:
 *   Up/Down  choose a letter     Right  type it     Left  delete     A or B  confirm (Enter)
 * helpLine() is shown in the 32X's bottom border while that screen is on, and for about two
 * seconds after a password request (so the player sees that the button arrived).
 */
#ifndef PAD_MAP_H
#define PAD_MAP_H

#include <stdint.h>
#include "../rawgl-master/systemstub.h"

struct PadMapper {
	uint16_t _prev;
	uint8_t _letter;       // 0..25 = 'A'..'Z'
	uint8_t _notice;       // R4b: calls left to show the password-request notice
	char _help[48];

	PadMapper() : _prev(0), _letter(0), _notice(0) { _help[0] = 0; }
	// pad: PAD_* bits (plat.h); passwordScreen: the game is on part 16008
	void apply(uint16_t pad, PlayerInput &pi, bool passwordScreen);
	// text for the 32X border, or 0 when nothing is to be shown;
	// hasPasswordScreen: the data has the password part (rawgl's _hasPasswordScreen)
	const char *helpLine(bool passwordScreen, bool hasPasswordScreen = true);
};

#endif
