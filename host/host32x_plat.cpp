/*
 * RAWGL32X - what the 32X platform provides, for the PC test build of the 32X engine
 * (rawgl_host32x): the data pack (mapped read-only, so any write into the game data
 * crashes the test instead of going unnoticed), the sound command path into the same
 * mixer the slave SH2 runs (aw_mixer.c), the clock, and the error/log hooks.
 */
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../rawgl-master/intern.h"
#include "../port/aw_mixer.h"
#include "host.h"

extern "C" const uint8_t *rawgl32x_pack(void) {
	static const uint8_t *pack;
	if (!pack) {
		const char *path = getenv("AW_PACK");
		if (!path) {
			fprintf(stderr, "AW_PACK is not set (the data pack made by aw_pack)\n");
			return 0;
		}
		const int fd = open(path, O_RDONLY);
		struct stat st;
		if (fd < 0 || fstat(fd, &st) != 0) {
			fprintf(stderr, "cannot open AW_PACK %s\n", path);
			return 0;
		}
		void *p = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		close(fd);
		if (p == MAP_FAILED) return 0;
		pack = (const uint8_t *)p;
	}
	return pack;
}

extern "C" uint32_t rawgl32x_ms(void) { return host_now(); }

extern "C" void plat_log(const char *msg) { (void)msg; }

extern "C" void plat_fatal(const char *msg) {
	fprintf(stderr, "ERROR: %s!\n", msg);
	exit(-1);
}

/* sound commands go straight to the mixer; host_mixAudio renders it into the WAV */
static FILE *g_wav;
static uint32_t g_wavFrames, g_frac;
static bool g_init;

static void wavHeader() {
	const uint32_t rate = MIXER_RATE, dataLen = g_wavFrames * 4, riff = 36 + dataLen;
	uint8_t h[44] = { 'R','I','F','F', (uint8_t)riff, (uint8_t)(riff >> 8), (uint8_t)(riff >> 16), (uint8_t)(riff >> 24),
		'W','A','V','E','f','m','t',' ', 16,0,0,0, 1,0, 2,0,
		(uint8_t)rate, (uint8_t)(rate >> 8), 0, 0,
		(uint8_t)(rate * 4), (uint8_t)((rate * 4) >> 8), (uint8_t)((rate * 4) >> 16), 0, 4,0, 16,0,
		'd','a','t','a', (uint8_t)dataLen, (uint8_t)(dataLen >> 8), (uint8_t)(dataLen >> 16), (uint8_t)(dataLen >> 24) };
	fseek(g_wav, 0, SEEK_SET);
	fwrite(h, 1, 44, g_wav);
	fseek(g_wav, 0, SEEK_END);
}

static void finish() {
	if (g_wav) {
		wavHeader();
		fclose(g_wav);
		g_wav = 0;
		host_logf("audio: %u samples written (%.1f s)\n", g_wavFrames, g_wavFrames / (double)MIXER_RATE);
	}
}

static void ensureInit() {
	if (!g_init) {
		g_init = true;
		mixer_init();
		const char *path = getenv("AW_AUDIO_WAV");
		if (path && (g_wav = fopen(path, "wb")) != 0) {
			wavHeader();
			atexit(finish);
		}
	}
}

extern "C" void plat_sound_command(const MixCmd *c) {
	ensureInit();
	mixer_command(c);
}

extern "C" void rawgl32x_audio_poll(void);   // port/mixer_32x.cpp

void host_mixAudio(uint32_t ms) {
	ensureInit();
	rawgl32x_audio_poll();   // a tick-delay change the script made goes to the mixer first
	const uint32_t total = g_frac + ms * MIXER_RATE;
	int n = total / 1000;
	g_frac = total % 1000;
	static int16_t st[8192];
	while (n > 0) {
		const int k = n > 4096 ? 4096 : n;
		mixer_render(st, k);   // stereo, as the slave SH2 renders it
		if (g_wav) {
			fwrite(st, sizeof(int16_t), 2 * k, g_wav);
			g_wavFrames += k;
		}
		n -= k;
	}
	rawgl32x_audio_poll();   // music sync events into the script variable, as rawgl's player does
}
