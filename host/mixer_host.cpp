/*
 * RAWGL32X host mixer - replaces mixer.cpp (SDL_mixer) with the mixer the 32X will run:
 * the four game channels (raw 8-bit sounds) plus SfxPlayer's module music, software-mixed
 * at 22050 Hz (the PWM rate Kobo and Tyrian use on the slave SH2).
 *
 * Same Mixer interface as the original, so script.cpp is unchanged.  Only the raw path
 * is implemented (DOS / Amiga / Atari data); the WAV / AIFF / MT-32 / OGG paths of the
 * later editions print one warning and do nothing.
 *
 * Mixing is pulled by the host clock: host_mixAudio(ms) mixes exactly the samples for
 * that much virtual time (fractional samples carried over), so the WAV lines up with the
 * frames.  Channel code is the original MixerChannel::initRaw / mixRaw.
 */
#include "../rawgl-master/mixer.h"
#include "../rawgl-master/sfxplayer.h"
#include "../rawgl-master/util.h"
#include "host.h"

static int16_t toS16(int a) {
	return ((a << 8) | a) - 32768;
}
static int16_t mixS16(int sample1, int sample2) {
	const int sample = sample1 + sample2;
	return sample < -32768 ? -32768 : ((sample > 32767 ? 32767 : sample));
}

struct MixerChannel {
	const uint8_t *_data;
	Frac _pos;
	uint32_t _len;
	uint32_t _loopLen, _loopPos;
	int _volume;

	void initRaw(const uint8_t *data, int freq, int volume, int mixingFreq) {
		_data = data + 8;
		_pos.reset(freq, mixingFreq);
		const int len = READ_BE_UINT16(data) * 2;
		_loopLen = READ_BE_UINT16(data + 2) * 2;
		_loopPos = _loopLen ? len : 0;
		_len = len;
		_volume = volume;
	}
	void mixRaw(int16_t &sample) {
		if (_data) {
			uint32_t pos = _pos.getInt();
			_pos.offset += _pos.inc;
			if (_loopLen != 0) {
				if (pos >= _loopPos + _loopLen) {
					pos = _loopPos;
					_pos.offset = (_loopPos << Frac::BITS) + _pos.inc;
				}
			} else {
				if (pos >= _len) {
					_data = 0;
					return;
				}
			}
			sample = mixS16(sample, toS16(_data[pos] ^ 0x80) * _volume / 64);
		}
	}
};

struct Mixer_impl {
	static const int kMixFreq = 22050;
	static const int kMixChannels = 4;
	MixerChannel _channels[kMixChannels];
	SfxPlayer *_sfx;
	FILE *_wav;
	uint32_t _wavFrames;
	uint32_t _fracAcc;      /* sample remainder, in 1/1000 sample */
	bool _warned;
};

static Mixer_impl *g_mixer;

static void wavHeader(FILE *f, uint32_t frames) {
	const uint32_t dataLen = frames * 4;
	uint8_t h[44];
	memcpy(h, "RIFF", 4);
	const uint32_t riffLen = 36 + dataLen;
	h[4] = riffLen; h[5] = riffLen >> 8; h[6] = riffLen >> 16; h[7] = riffLen >> 24;
	memcpy(h + 8, "WAVEfmt ", 8);
	const uint8_t fmt[16] = { 16, 0, 0, 0, 1, 0, 2, 0,
		(uint8_t)(Mixer_impl::kMixFreq), (uint8_t)(Mixer_impl::kMixFreq >> 8), 0, 0,
		(uint8_t)(Mixer_impl::kMixFreq * 4), (uint8_t)((Mixer_impl::kMixFreq * 4) >> 8), (uint8_t)((Mixer_impl::kMixFreq * 4) >> 16), 0 };
	memcpy(h + 16, fmt, 16);
	h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
	memcpy(h + 36, "data", 4);
	h[40] = dataLen; h[41] = dataLen >> 8; h[42] = dataLen >> 16; h[43] = dataLen >> 24;
	fseek(f, 0, SEEK_SET);
	fwrite(h, 1, 44, f);
	fseek(f, 0, SEEK_END);
}

void host_mixAudio(uint32_t ms) {
	Mixer_impl *m = g_mixer;
	if (!m) return;
	const uint32_t total = m->_fracAcc + ms * Mixer_impl::kMixFreq;
	int frames = total / 1000;
	m->_fracAcc = total % 1000;
	static int16_t buf[2 * 4096];
	while (frames > 0) {
		const int n = frames > 4096 ? 4096 : frames;
		memset(buf, 0, n * 2 * sizeof(int16_t));
		for (int i = 0; i < n * 2; i += 2) {
			for (int j = 0; j < Mixer_impl::kMixChannels; ++j) {
				m->_channels[j].mixRaw(buf[i]);
			}
			buf[i + 1] = buf[i];
		}
		if (m->_sfx) {
			m->_sfx->readSamples(buf, n * 2);
		}
		if (m->_wav) {
			fwrite(buf, sizeof(int16_t), n * 2, m->_wav);   /* x86: little-endian already */
			m->_wavFrames += n;
		}
		frames -= n;
	}
}

static void unsupported(const char *what) {
	if (g_mixer && !g_mixer->_warned) {
		warning("host mixer: %s is not supported (only the raw DOS/Amiga sounds are)", what);
		g_mixer->_warned = true;
	}
}

Mixer::Mixer(SfxPlayer *sfx) : _aifc(0), _sfx(sfx), _impl(0) {
}

void Mixer::init(MixerType mixerType) {
	_impl = new Mixer_impl();
	memset(_impl, 0, sizeof(*_impl));
	g_mixer = _impl;
	if (mixerType != kMixerTypeRaw) {
		unsupported("this data version's audio");
	}
	const char *path = getenv("AW_AUDIO_WAV");
	if (path) {
		_impl->_wav = fopen(path, "wb");
		if (_impl->_wav) wavHeader(_impl->_wav, 0);
		else warning("cannot write %s", path);
	}
}

void Mixer::quit() {
	stopAll();
	if (_impl) {
		if (_impl->_wav) {
			wavHeader(_impl->_wav, _impl->_wavFrames);
			fclose(_impl->_wav);
			host_logf("audio: %u samples written (%.1f s)\n", _impl->_wavFrames, _impl->_wavFrames / (double)Mixer_impl::kMixFreq);
		}
		g_mixer = 0;
		delete _impl;
		_impl = 0;
	}
}

void Mixer::update() {
}

bool Mixer::hasMt32() const { return false; }
bool Mixer::hasMt32SoundMapping(int num) { return false; }

void Mixer::playSoundRaw(uint8_t channel, const uint8_t *data, uint16_t freq, uint8_t volume) {
	debug(DBG_SND, "Mixer::playSoundRaw(%d, %d, %d)", channel, freq, volume);
	if (_impl) _impl->_channels[channel].initRaw(data, freq, volume, Mixer_impl::kMixFreq);
}
void Mixer::playSoundWav(uint8_t, const uint8_t *, uint16_t, uint8_t, uint8_t) { unsupported("WAV sound"); }
void Mixer::stopSound(uint8_t channel) {
	if (_impl) _impl->_channels[channel]._data = 0;
}
void Mixer::playSoundMt32(int) { unsupported("MT-32"); }
void Mixer::setChannelVolume(uint8_t channel, uint8_t volume) {
	if (_impl) _impl->_channels[channel]._volume = volume;
}
void Mixer::playMusic(const char *, uint8_t) { unsupported("music file"); }
void Mixer::stopMusic() {}
void Mixer::playAifcMusic(const char *, uint32_t) { unsupported("AIFC music"); }
void Mixer::stopAifcMusic() {}
void Mixer::playSfxMusic(int num) {
	debug(DBG_SND, "Mixer::playSfxMusic(%d)", num);
	if (_impl && _sfx) {
		_impl->_sfx = _sfx;
		_sfx->play(Mixer_impl::kMixFreq);
	}
}
void Mixer::stopSfxMusic() {
	if (_impl && _impl->_sfx) {
		_impl->_sfx->stop();
		_impl->_sfx = 0;
	}
}
void Mixer::stopAll() {
	if (!_impl) return;
	for (int i = 0; i < Mixer_impl::kMixChannels; ++i) stopSound(i);
	stopSfxMusic();
}
void Mixer::preloadSoundAiff(uint8_t, const uint8_t *) { unsupported("AIFF sound"); }
void Mixer::playSoundAiff(uint8_t, uint8_t, uint8_t) { unsupported("AIFF sound"); }
