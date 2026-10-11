/*
 * RAWGL32X - main() on the 32X master SH2 (called by plat_mars_entry after the hardware
 * set-up). rawgl's main.cpp minus the command line and SDL: the engine reads the data pack
 * in ROM, draws with the 4-bpp renderer and runs forever (a console never quits).
 */
#include "../rawgl-master/engine.h"
#include "../rawgl-master/graphics.h"
#include "../rawgl-master/systemstub.h"
#include "../rawgl-master/util.h"
#include "plat.h"

Graphics *Graphics4bpp_create();
SystemStub *SystemStub_32X_create();
void rawgl32x_run_ctors();

const uint16_t *rawgl32x_part;   // the game part being played (systemstub_32x.cpp)
const bool *rawgl32x_haspw;      // the data has the password screen (R4b notice)

// R4: credits at power-on, 5 s or until START / A / B / C
static const char *const kCredits[] = {
	"ANOTHER WORLD 32X",
	"",
	"ANOTHER WORLD: ERIC CHAHI",
	"RAWGL ENGINE: GREGORY MONTOIR",
	"D32XR 32X CODE: VICTOR LUCHITS",
	"CD AND 32X FRAMEWORK: CHILLY WILLY",
	"SEGA 32X PORT: MICRONUT99",
	"",
	"PAD: A/B RUN KICK SHOOT  C/UP JUMP",
	"START PAUSE  MODE OR A+B+C PASSWORD",
	"",
	"PRESS START",
};

extern "C" const uint8_t *rawgl32x_pack(void) {
	return plat_romfs_base();
}

extern "C" uint32_t rawgl32x_ms(void) {
	return plat_ticks_ms();
}

int main(int argc, char *argv[]) {
	rawgl32x_run_ctors();
	plat_init();
	g_debugMask = 0;
	plat_log("ENGINE START");
	if (!rawgl32x_pack()) {
		plat_fatal("DATA PACK (AWPK) NOT FOUND IN THE ROM");
	}
	plat_text_pause(kCredits, sizeof(kCredits) / sizeof(kCredits[0]), 5000);
	Graphics::_is1991 = true;              // the original 1991 look, 320x200, 16 colours
#ifndef RAWGL32X_START_PART
#define RAWGL32X_START_PART 16001   // the introduction
#endif
	Engine *e = new Engine(0, RAWGL32X_START_PART);  // R2c: START=16002 ./build.sh starts in the water level
	Graphics *graphics = Graphics4bpp_create();
	SystemStub *stub = SystemStub_32X_create();
	DisplayMode dm;
	dm.mode = DisplayMode::WINDOWED; dm.width = 320; dm.height = 200; dm.opengl = false;
	stub->init(e->getGameTitle(LANG_US), &dm);
	e->setSystemStub(stub, graphics);
	rawgl32x_part = &e->_res._currentPart;
	rawgl32x_haspw = &e->_res._hasPasswordScreen;
	e->setup(LANG_US, GRAPHICS_ORIGINAL, "", 1, false);
	plat_log("ENGINE RUNNING");
	for (;;) {
		e->run();
	}
	return 0;
}
