/*
 * RAWGL32X host platform layer (PC, no SDL) - the test harness for the 32X port.
 *
 * Replaces systemstub_sdl.cpp.  Everything is driven by environment variables so a run
 * is a single command line and repeats frame for frame:
 *
 *   AW_PAD="t5000:A,t5200:-"   scripted pad: at 5.0 s press A, at 5.2 s release all.
 *                              Buttons: L R U D A B C S M (S = START, M = MODE), '-' = none.
 *                              They go through the 32X's own mapping (port/pad_map.cpp).
 *                              Several at once: t9000:RA  (right + A).
 *   AW_MAX_SECONDS / AW_MAX_FRAMES   stop the run (virtual time / shown frames).
 *   AW_FRAMES_DIR + AW_SAVE_EVERY    save every Nth shown frame as PNG (320x200).
 *   AW_AUDIO_WAV               write what the mixer produces (22050 Hz stereo 16-bit).
 *
 * VIRTUAL CLOCK: getTimeStamp() returns virtual milliseconds; only sleep() moves time.
 * The game paces itself with sleep(), so a frame lasts exactly what the script asked.
 * If a frame is shown without any sleep since the last one, 20 ms (one 50 Hz tick) is
 * added so time always moves.  Audio for the elapsed time is mixed inside sleep().
 */
#include <zlib.h>
#include "../rawgl-master/systemstub.h"
#include "../rawgl-master/util.h"
#include "host.h"
#include "../port/pad_map.h"
#include "../port/plat.h"

const uint16_t *rawgl32x_part;   // main_host.cpp: the game part being played (password screen)

struct PadEvent { uint32_t t; uint16_t mask; };
/* script buttons (K_*; the PAD_* names are the 32X pad bits from plat.h) */
enum { K_L = 1, K_R = 2, K_U = 4, K_D = 8, K_A = 16, K_B = 32, K_C = 64, K_S = 128, K_M = 256 };

/* ---- PNG writer (zlib only) ---------------------------------------------------------- */
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void pngChunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
	uint8_t hdr[8]; put32(hdr, len); memcpy(hdr + 4, type, 4);
	fwrite(hdr, 1, 8, f);
	if (len) fwrite(data, 1, len, f);
	uLong crc = crc32(0, (const Bytef *)type, 4);
	if (len) crc = crc32(crc, data, len);
	uint8_t c[4]; put32(c, (uint32_t)crc); fwrite(c, 1, 4, f);
}
bool host_writePNG(const char *path, const uint16_t *rgb555, int w, int h) {
	FILE *f = fopen(path, "wb");
	if (!f) return false;
	static const uint8_t sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
	fwrite(sig, 1, 8, f);
	uint8_t ihdr[13]; put32(ihdr, w); put32(ihdr + 4, h);
	ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
	pngChunk(f, "IHDR", ihdr, 13);
	const uLong rawLen = (uLong)h * (w * 3 + 1);
	uint8_t *raw = (uint8_t *)malloc(rawLen);
	uint8_t *p = raw;
	for (int y = 0; y < h; ++y) {
		*p++ = 0;
		for (int x = 0; x < w; ++x) {
			const uint16_t c = rgb555[y * w + x];
			const int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
			*p++ = (r << 3) | (r >> 2); *p++ = (g << 3) | (g >> 2); *p++ = (b << 3) | (b >> 2);
		}
	}
	uLong zLen = compressBound(rawLen);
	uint8_t *z = (uint8_t *)malloc(zLen);
	compress2(z, &zLen, raw, rawLen, 6);
	pngChunk(f, "IDAT", z, zLen);
	pngChunk(f, "IEND", 0, 0);
	free(z); free(raw);
	fclose(f);
	return true;
}

/* ---- pad script ---------------------------------------------------------------------- */
int host_parsePad(const char *s, PadEvent *ev, int maxEv) {
	int n = 0;
	while (s && *s && n < maxEv) {
		if (*s == ',' || *s == ' ') { ++s; continue; }
		if (*s != 't') { warning("AW_PAD: expected 't' at '%s'", s); return -1; }
		char *end;
		const unsigned long t = strtoul(s + 1, &end, 10);
		if (*end != ':') { warning("AW_PAD: expected ':' at '%s'", end); return -1; }
		s = end + 1;
		uint16_t m = 0;
		while (*s && *s != ',') {
			switch (*s) {
			case 'L': m |= K_L; break; case 'R': m |= K_R; break;
			case 'U': m |= K_U; break; case 'D': m |= K_D; break;
			case 'A': m |= K_A; break; case 'B': m |= K_B; break;
			case 'C': m |= K_C; break; case 'S': m |= K_S; break;
			case 'M': m |= K_M; break;
			case '-': break;
			default: warning("AW_PAD: unknown button '%c'", *s); return -1;
			}
			++s;
		}
		ev[n].t = (uint32_t)t; ev[n].mask = m; ++n;
	}
	return n;
}

static uint32_t g_hostNow;  /* virtual clock, for host_now() */
uint32_t host_now() { return g_hostNow; }

struct SystemStub_Host : SystemStub {
	enum { kMaxEvents = 4096 };
	uint32_t _now, _lastFrameTime;
	uint32_t _frames, _maxFrames, _maxMs, _saveEvery;
	const char *_framesDir;
	PadEvent _ev[kMaxEvents];
	int _numEv, _nextEv;
	uint16_t _pad;
	PadMapper _map;
	uint16_t _screen[320 * 200];
	int _screenW, _screenH;

	virtual void init(const char *title, const DisplayMode *dm) {
		_now = _lastFrameTime = 0;
		_frames = 0;
		_maxFrames = getenv("AW_MAX_FRAMES") ? atoi(getenv("AW_MAX_FRAMES")) : 0;
		_maxMs = getenv("AW_MAX_SECONDS") ? atoi(getenv("AW_MAX_SECONDS")) * 1000 : 0;
		_framesDir = getenv("AW_FRAMES_DIR");
		_saveEvery = getenv("AW_SAVE_EVERY") ? atoi(getenv("AW_SAVE_EVERY")) : 1;
		if (_saveEvery == 0) _saveEvery = 1;
		_numEv = 0; _nextEv = 0;
		if (getenv("AW_PAD")) {
			_numEv = host_parsePad(getenv("AW_PAD"), _ev, kMaxEvents);
			if (_numEv < 0) error("AW_PAD could not be read");
		}
		_pad = 0;
		_screenW = _screenH = 0;
		if (!_maxFrames && !_maxMs) {
			warning("neither AW_MAX_FRAMES nor AW_MAX_SECONDS set: stopping after 60 s");
			_maxMs = 60000;
		}
		debug(DBG_INFO, "host: '%s' max %u frames / %u ms", title, _maxFrames, _maxMs);
	}
	virtual void fini() {
		host_logf("host: end at %u ms, %u frames shown\n", _now, _frames);
	}
	virtual void prepareScreen(int &w, int &h, float ar[4]) {
		w = 320; h = 200; ar[0] = ar[1] = 0.f; ar[2] = ar[3] = 1.f;
	}
	virtual void updateScreen() {
		if (_now == _lastFrameTime) {
			advance(20);
		}
		_lastFrameTime = _now;
		if (_framesDir && (_frames % _saveEvery) == 0 && _screenW == 320 && _screenH == 200) {
			char path[512];
			snprintf(path, sizeof(path), "%s/f%06u.png", _framesDir, _frames);
			if (!host_writePNG(path, _screen, 320, 200)) warning("cannot write %s", path);
		}
		host_onFrame(_frames, _now);
		++_frames;
		if ((_maxFrames && _frames >= _maxFrames) || (_maxMs && _now >= _maxMs)) {
			_pi.quit = true;
		}
	}
	virtual void setScreenPixels555(const uint16_t *data, int w, int h) {
		if (w == 320 && h == 200) {
			memcpy(_screen, data, sizeof(_screen));
		}
		_screenW = w; _screenH = h;
	}
	virtual void processEvents() {
		while (_nextEv < _numEv && _ev[_nextEv].t <= _now) {
			_pad = _ev[_nextEv].mask;
			++_nextEv;
			static const char kNames[] = "LRUDABCSM";
			char b[10]; int k = 0;
			for (int i = 0; i < 9; ++i) if (_pad & (1 << i)) b[k++] = kNames[i];
			b[k] = 0;
			host_logf("pad %s at %u ms (frame %u)\n", k ? b : "-", _now, _frames);
		}
		/* the script's buttons -> the 32X pad bits -> the 32X's mapping */
		uint16_t pad = 0;
		if (_pad & K_L) pad |= PAD_LEFT;
		if (_pad & K_R) pad |= PAD_RIGHT;
		if (_pad & K_U) pad |= PAD_UP;
		if (_pad & K_D) pad |= PAD_DOWN;
		if (_pad & K_A) pad |= PAD_A;
		if (_pad & K_B) pad |= PAD_B;
		if (_pad & K_C) pad |= PAD_C;
		if (_pad & K_S) pad |= PAD_START;
		if (_pad & K_M) pad |= PAD_MODE;
		const bool pw = rawgl32x_part && *rawgl32x_part == 16008;
		const char prevChar = _pi.lastChar;
		const bool prevCode = _pi.code;
		_map.apply(pad, _pi, pw);
		if (_pi.code && !prevCode)
			host_logf("password screen requested at %u ms\n", _now);
		if (_pi.lastChar && _pi.lastChar != prevChar)
			host_logf("password key %d ('%c') at %u ms\n", _pi.lastChar, _pi.lastChar >= 'a' ? _pi.lastChar : '-', _now);
		if (_maxMs && _now >= _maxMs) _pi.quit = true;
	}
	void advance(uint32_t ms) {
		host_mixAudio(ms);
		_now += ms;
		g_hostNow = _now;
	}
	virtual void sleep(uint32_t duration) { advance(duration); }
	virtual uint32_t getTimeStamp() { return _now; }
};

SystemStub *SystemStub_Host_create() {
	return new SystemStub_Host();
}
