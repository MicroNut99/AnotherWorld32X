/*
 * RAWGL32X host main - replaces main.cpp (SDL).  Runs the unchanged engine headless with
 * the original 1991 software renderer and prints a memory report at the end:
 * for every game part, the most memory the part's resources ever used at once
 * (the original engine's single resource block, _scriptCurPtr - _memPtrStart).
 * That number decides what has to live in SDRAM on the 32X.
 *
 *   rawgl_host --datapath=DIR [--part=16001] [--language=us|fr]
 *   AW_LOG=file  also writes the report lines to a file.
 *
 * Built twice (host/Makefile): rawgl_host = the unchanged engine reading the original data
 * files with rawgl's own renderer and mixer (the reference); rawgl_host32x = the engine as
 * built for the 32X (-DRAWGL32X: data pack used in place, 4-bpp renderer, 32X mixer), which
 * reads the pack from AW_PACK.  Same pad script -> the frames must be identical.
 */
#include <stdarg.h>
#include <getopt.h>
#include "../rawgl-master/engine.h"
#include "../rawgl-master/graphics.h"
#include "../rawgl-master/resource.h"
#include "../rawgl-master/systemstub.h"
#include "../rawgl-master/util.h"
#include "host.h"

static FILE *g_log;
static Engine *g_engine;

struct PartStat { uint32_t peak, frames, firstMs; };
static PartStat g_parts[10];

void host_logf(const char *fmt, ...) {
	va_list va;
	va_start(va, fmt); vprintf(fmt, va); va_end(va);
	if (g_log) { va_start(va, fmt); vfprintf(g_log, fmt, va); va_end(va); fflush(g_log); }
}

static void sample(uint32_t ms) {
	Resource &r = g_engine->_res;
	const int part = r._currentPart - 16000;
	if (part < 0 || part > 9 || !r._memPtrStart) return;
#ifdef RAWGL32X
	const uint32_t used = r._romUsed;   /* same accounting, resources used in place from the pack */
#else
	const uint32_t used = (uint32_t)(r._scriptCurPtr - r._memPtrStart);
#endif
	PartStat &p = g_parts[part];
	if (p.frames == 0) {
		p.firstMs = ms;
		host_logf("part %d starts at %u ms\n", r._currentPart, ms);
	}
	++p.frames;
	if (used > p.peak) p.peak = used;
}

void host_onFrame(uint32_t frame, uint32_t ms) {
	if (g_engine) sample(ms);
}

int main(int argc, char *argv[]) {
	const char *dataPath = ".";
	int part = 16001;
	Language lang = LANG_US;
	for (;;) {
		static struct option options[] = {
			{ "datapath", required_argument, 0, 'd' },
			{ "part",     required_argument, 0, 'p' },
			{ "language", required_argument, 0, 'l' },
			{ 0, 0, 0, 0 }
		};
		int index;
		const int c = getopt_long(argc, argv, "", options, &index);
		if (c == -1) break;
		switch (c) {
		case 'd': dataPath = optarg; break;
		case 'p': part = atoi(optarg); break;
		case 'l': lang = (strcmp(optarg, "fr") == 0) ? LANG_FR : LANG_US; break;
		default:
			printf("Usage: %s --datapath=DIR [--part=NUM] [--language=us|fr]\n", argv[0]);
			return 1;
		}
	}
	if (getenv("AW_LOG")) g_log = fopen(getenv("AW_LOG"), "w");
	g_debugMask = DBG_INFO;
	Engine *e = new Engine(dataPath, part);
	g_engine = e;
	Graphics::_is1991 = true;              /* "original" renderer: 320x200, 16 colours */
#ifdef RAWGL32X
	Graphics *Graphics4bpp_create();
	Graphics *graphics = Graphics4bpp_create();   /* the 32X renderer (port/graphics_4bpp.cpp) */
#else
	Graphics *graphics = GraphicsSoft_create();
#endif
	SystemStub *stub = SystemStub_Host_create();
	DisplayMode dm;
	dm.mode = DisplayMode::WINDOWED; dm.width = 320; dm.height = 200; dm.opengl = false;
	stub->init(e->getGameTitle(lang), &dm);
	e->setSystemStub(stub, graphics);
	extern const uint16_t *rawgl32x_part;
	rawgl32x_part = &e->_res._currentPart;
	e->setup(lang, GRAPHICS_ORIGINAL, "", 1, false);
	host_logf("data type %d\n", (int)e->_res.getDataType());
	while (!stub->_pi.quit) {
		e->run();
	}
	host_logf("--- memory per part (peak of the original resource block, bitmap area excluded) ---\n");
	for (int i = 0; i < 10; ++i) {
		if (g_parts[i].frames) {
			host_logf("part %d: peak %u bytes (%.1f KB), %u frames, from %u ms\n", 16000 + i,
				g_parts[i].peak, g_parts[i].peak / 1024.0, g_parts[i].frames, g_parts[i].firstMs);
		}
	}
	e->finish();
	stub->fini();
	g_engine = 0;
	delete e;
	delete graphics;
	delete stub;
	if (g_log) { fclose(g_log); g_log = 0; }
	return 0;
}
