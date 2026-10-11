/*
 * RAWGL32X - rawgl's SystemStub on the 32X: time from the SH2 watchdog clock, the pad from
 * the 68000 (plat_read_pad, command 48) through the shared mapping (pad_map.h, also used by
 * the PC harness). The picture does not pass through here: the renderer (graphics_4bpp.cpp)
 * hands its page to plat_present_4bpp() directly. On the password screen the help line
 * (letter chooser) goes to the bottom border (plat_set_status).
 */
#include "../rawgl-master/systemstub.h"
#include "pad_map.h"
#include "plat.h"

extern "C" void rawgl32x_audio_poll(void);   // mixer_32x.cpp: music delay changes, sync events
extern const uint16_t *rawgl32x_part;        // main_32x.cpp: the game part being played
extern const bool *rawgl32x_haspw;           // main_32x.cpp: the data has the password screen

struct SystemStub_32X : SystemStub {
	PadMapper _map;

	virtual void init(const char *title, const DisplayMode *dm) {
		memset(&_pi, 0, sizeof(_pi));
	}
	virtual void fini() {}
	virtual void prepareScreen(int &w, int &h, float ar[4]) {
		w = 320; h = 200; ar[0] = ar[1] = 0.f; ar[2] = ar[3] = 1.f;
	}
	virtual void updateScreen() {}
	virtual void setScreenPixels555(const uint16_t *data, int w, int h) {}
	virtual void processEvents() {
		rawgl32x_audio_poll();
		const bool pw = rawgl32x_part && *rawgl32x_part == 16008;
		_map.apply(plat_read_pad(), _pi, pw);
		plat_set_status(_map.helpLine(pw, rawgl32x_haspw && *rawgl32x_haspw));
	}
	virtual void sleep(uint32_t duration) {
		const uint32_t start = plat_ticks_ms();
		rawgl32x_audio_poll();
		while (plat_ticks_ms() - start < duration) {
			rawgl32x_audio_poll();   // the music's sync events reach the script while it waits
		}
	}
	virtual uint32_t getTimeStamp() {
		return plat_ticks_ms();
	}
};

SystemStub *SystemStub_32X_create() {
	return new SystemStub_32X();
}
