/*
 * RAWGL32X - rawgl's Mixer interface for the 32X (replaces mixer.cpp / SDL_mixer).
 *
 * Everything audible is mixed by the slave SH2 (aw_mixer.c); this side only sends commands
 * through plat_sound_command (32X: a ring in uncached SDRAM; PC test build: straight into the
 * same mixer). Sound data and music modules stay in ROM; only pointers travel.
 *
 * Music (R3): the script loads the module into the master's SfxPlayer as in rawgl
 * (loadSfxModule resolves the instruments); playSfxMusic hands the module, its start order,
 * tick delay and instruments to the slave, which plays it at sample precision.
 * rawgl32x_audio_poll() - called by the platform before and after mixing (PC) and while the
 * master waits (32X) - forwards a tick-delay change the script made (setEventsDelay goes to
 * SfxPlayer directly, not through the Mixer) and copies the slave's music sync events into
 * the script variable (VAR_MUSIC_SYNC), as rawgl's player writes it.
 */
#include "../rawgl-master/mixer.h"
#include "../rawgl-master/sfxplayer.h"
#include "../rawgl-master/util.h"
#include "aw_mixer.h"

extern "C" void plat_sound_command(const MixCmd *c);
// 32X: address | 0x20000000 (cache-through); PC: the address itself
static inline void *rawgl32x_uncached(void *p) {
#ifdef RAWGL32X_MARS
	return (void *)((uintptr_t)p | 0x20000000u);
#else
	return p;
#endif
}

struct Mixer_impl {
	bool musicOn;
	uint16_t sentDelay;    // tick delay the slave has
	uint32_t syncSeq;      // last sync event copied into the script variable
	bool warned;
};
static Mixer_impl g_impl;
static SfxPlayer *g_sfx;
// written here, read by the slave through the cache-through alias (aw_mixer.c)
static MixInstruments g_instruments;

static void send(uint8_t op, uint8_t chan, uint8_t vol, uint16_t freq, const uint8_t *data,
                 const MixInstruments *ext = 0) {
	MixCmd c;
	memset(&c, 0, sizeof(c));
	c.op = op; c.chan = chan; c.vol = vol; c.freq = freq; c.data = data; c.ext = ext;
	plat_sound_command(&c);
}

extern "C" void rawgl32x_audio_poll(void) {
	if (!g_sfx) return;
	if (g_impl.musicOn && g_sfx->_delay != g_impl.sentDelay) {
		g_impl.sentDelay = g_sfx->_delay;
		send(MIX_MUSIC_DELAY, 0, 0, g_impl.sentDelay, 0);
	}
	int16_t v;
	const uint32_t seq = mixer_sync_read(&v);
	if (seq != g_impl.syncSeq) {
		g_impl.syncSeq = seq;
		*g_sfx->_syncVar = v;
	}
}

static void unsupported(const char *what) {
	if (!g_impl.warned) {
		warning("32X mixer: %s not supported", what);
		g_impl.warned = true;
	}
}

Mixer::Mixer(SfxPlayer *sfx) : _aifc(0), _sfx(sfx), _impl(0) {
}

void Mixer::init(MixerType mixerType) {
	_impl = &g_impl;
	memset(_impl, 0, sizeof(*_impl));
	g_sfx = _sfx;
	int16_t v;
	_impl->syncSeq = mixer_sync_read(&v);
	if (mixerType != kMixerTypeRaw) {
		unsupported("this data version's audio");
	}
	send(MIX_STOP_ALL, 0, 0, 0, 0);
}

void Mixer::quit() {
	stopAll();
	_impl = 0;
}

// once per game frame (Engine::run)
void Mixer::update() {
	rawgl32x_audio_poll();
}

bool Mixer::hasMt32() const { return false; }
bool Mixer::hasMt32SoundMapping(int num) { return false; }

void Mixer::playSoundRaw(uint8_t channel, const uint8_t *data, uint16_t freq, uint8_t volume) {
	debug(DBG_SND, "Mixer::playSoundRaw(%d, %d, %d)", channel, freq, volume);
	send(MIX_PLAY, channel, volume, freq, data);
}
void Mixer::playSoundWav(uint8_t, const uint8_t *, uint16_t, uint8_t, uint8_t) { unsupported("WAV sound"); }
void Mixer::stopSound(uint8_t channel) { send(MIX_STOP, channel, 0, 0, 0); }
void Mixer::playSoundMt32(int) { unsupported("MT-32"); }
void Mixer::setChannelVolume(uint8_t channel, uint8_t volume) { send(MIX_VOLUME, channel, volume, 0, 0); }
void Mixer::playMusic(const char *, uint8_t) { unsupported("music file"); }
void Mixer::stopMusic() {}
void Mixer::playAifcMusic(const char *, uint32_t) { unsupported("AIFC music"); }
void Mixer::stopAifcMusic() {}
void Mixer::playSfxMusic(int num) {
	debug(DBG_SND, "Mixer::playSfxMusic(%d)", num);
	if (!_impl || !_sfx) return;
	_sfx->play(MIXER_RATE);   // master copy: rate, channels cleared (as rawgl)
	// the instruments loadSfxModule resolved; written through the cache-through alias on the 32X
	volatile MixInstruments *ins = (volatile MixInstruments *)rawgl32x_uncached(&g_instruments);
	for (int i = 0; i < 15; ++i) {
		ins->data[i] = _sfx->_sfxMod.samples[i].data;
		ins->volume[i] = _sfx->_sfxMod.samples[i].volume;
	}
	const uint8_t *module = _sfx->_sfxMod.orderTable ? _sfx->_sfxMod.orderTable - 0x40 : 0;
	if (!module) {
		warning("Mixer::playSfxMusic(%d): no module loaded", num);
		return;
	}
	_impl->musicOn = true;
	_impl->sentDelay = _sfx->_delay;
	send(MIX_MUSIC_START, 0, _sfx->_sfxMod.curOrder, _sfx->_delay, module, &g_instruments);
}
void Mixer::stopSfxMusic() {
	if (_impl && _impl->musicOn) {
		_sfx->stop();
		_impl->musicOn = false;
		send(MIX_MUSIC_STOP, 0, 0, 0, 0);
	}
}
void Mixer::stopAll() {
	if (!_impl) return;
	send(MIX_STOP_ALL, 0, 0, 0, 0);
	stopSfxMusic();
}
void Mixer::preloadSoundAiff(uint8_t, const uint8_t *) { unsupported("AIFF sound"); }
void Mixer::playSoundAiff(uint8_t, uint8_t, uint8_t) { unsupported("AIFF sound"); }
