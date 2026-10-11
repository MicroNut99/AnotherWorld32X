/*
 * RAWGL32X - sound mixer and music player (see aw_mixer.h).  Integer only (no FPU).
 *
 * Effects: rawgl's MixerChannel::initRaw / mixRaw (mixer.cpp).  Position: integer part +
 * 16-bit fraction in two words (sounds can be longer than 64 KB).
 * Music: rawgl's SfxPlayer (sfxplayer.cpp) - handleEvents, handlePattern, mixChannel and the
 * tick arithmetic of mixSamples - with a 64-bit position, as rawgl's Frac.
 */
#include "aw_mixer.h"

#ifdef RAWGL32X_MARS
/* data written by the other SH2 is read through the cache-through alias */
#define UNCACHED(p) ((volatile __typeof__(*(p)) *)((uintptr_t)(p) | 0x20000000u))
#else
#define UNCACHED(p) (p)
#endif

#define PAULA_FREQ 7159092

/* ------------------------------------------------------------------ effects */
typedef struct
{
	const uint8_t *data;   /* first sample (resource + 8); 0 = channel idle */
	uint32_t pos;          /* integer position */
	uint32_t frac;         /* 16-bit fraction */
	uint32_t inc;          /* 16.16 step */
	uint32_t len, loopPos, loopLen;
	int volume;
} Channel;

static Channel ch[MIXER_CHANNELS];

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static int clip16(int v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); }

/* ------------------------------------------------------------------ music */
typedef struct
{
	const int8_t *data;    /* sampleData */
	uint32_t len;          /* sampleLen (0 = idle) */
	uint32_t loopPos, loopLen;
	int volume;
	uint64_t offset;       /* rawgl Frac: 16.16 */
	uint32_t inc;
} MusicChannel;

static struct
{
	int on;                        /* rawgl: Mixer_impl::_sfx != 0 */
	const uint8_t *orderTable, *patterns;
	uint8_t curOrder, numOrder;
	uint16_t curPos;
	uint16_t delay;
	int samplesLeft;
	const uint8_t *insData[15];
	uint16_t insVolume[15];
	MusicChannel mc[4];
} mus;

static volatile uint32_t sync_seq;
static volatile int16_t sync_val;

uint32_t mixer_sync_read(int16_t *value)
{
	/* master side: both written by the slave (write-through cache), read uncached */
	uint32_t s;
	do
	{
		s = *UNCACHED(&sync_seq);
		*value = *UNCACHED(&sync_val);
	} while (s != *UNCACHED(&sync_seq));
	return s;
}

static void music_pattern(int channel, const uint8_t *data)
{
	const uint16_t note1 = be16(data), note2 = be16(data + 2);
	const uint8_t *buf = 0;
	uint32_t len = 0, loopPos = 0, loopLen = 0;
	int vol = 0;
	if (note1 != 0xFFFD)
	{
		const int sample = (note2 & 0xF000) >> 12;
		if (sample != 0 && mus.insData[sample - 1] != 0)
		{
			const uint8_t *ptr = mus.insData[sample - 1];
			vol = mus.insVolume[sample - 1];
			buf = ptr;
			len = (uint32_t)be16(ptr) * 2;
			const uint32_t ll = (uint32_t)be16(ptr + 2) * 2;
			if (ll != 0)
			{
				loopPos = len;
				loopLen = ll;
			}
			int m = vol;
			const int effect = (note2 & 0x0F00) >> 8;
			if (effect == 5)
			{
				m += note2 & 0xFF;
				if (m > 0x3F) m = 0x3F;
			}
			else if (effect == 6)
			{
				m -= note2 & 0xFF;
				if (m < 0) m = 0;
			}
			mus.mc[channel].volume = m;
			vol = m;
		}
	}
	if (note1 == 0xFFFD)
	{
		sync_val = (int16_t)note2;   /* rawgl: *_syncVar = note_2 */
		sync_seq = sync_seq + 1;
	}
	else if (note1 == 0xFFFE)
	{
		mus.mc[channel].len = 0;
	}
	else if (note1 != 0 && buf != 0)
	{
		const int freq = PAULA_FREQ / (note1 * 2);
		MusicChannel *c = &mus.mc[channel];
		c->data = (const int8_t *)(buf + 8);
		c->len = len;
		c->loopPos = loopPos;
		c->loopLen = loopLen;
		c->volume = vol;
		c->offset = 0;
		/* rawgl: (freq << 16) / _rate in int - wraps for freq > 32767, kept as in rawgl */
		c->inc = (uint32_t)((int32_t)((uint32_t)freq << 16) / MIXER_RATE);
	}
}

static void music_events(void)
{
	uint8_t order = mus.orderTable[mus.curOrder];
	const uint8_t *p = mus.patterns + mus.curPos + order * 1024;
	for (int c = 0; c < 4; ++c)
	{
		music_pattern(c, p);
		p += 4;
	}
	mus.curPos += 16;
	if (mus.curPos >= 1024)
	{
		mus.curPos = 0;
		mus.curOrder = (uint8_t)(mus.curOrder + 1);   /* rawgl plays on past numOrder */
	}
}

static int s16(int a)   /* sfxplayer.cpp toS16 */
{
	if (a <= -128) return -32768;
	if (a >= 127) return 32767;
	const int u8 = (a ^ 0x80) & 0xFF;
	return ((u8 << 8) | u8) - 32768;
}

static void music_mix(int16_t *s, MusicChannel *c)
{
	if (c->len == 0)
		return;
	int pos1 = (int)(c->offset >> 16);
	c->offset += c->inc;
	int pos2 = pos1 + 1;
	if (c->loopLen != 0)
	{
		if (pos1 >= (int)(c->loopPos + c->loopLen - 1))
		{
			pos2 = (int)c->loopPos;
			c->offset = (uint64_t)pos2 << 16;
		}
	}
	else if (pos1 >= (int)c->len - 1)
	{
		c->len = 0;
		return;
	}
	/* rawgl Frac::interpolate uses the fraction AFTER the step above */
	const int frac = (int)(c->offset & 0xFFFF);
	const int sample = (c->data[pos1] * (0xFFFF - frac) + c->data[pos2] * frac) >> 16;
	*s = (int16_t)clip16(*s + s16(sample * c->volume / 64));
}

/* ------------------------------------------------------------------ commands */
void mixer_init(void)
{
	for (int i = 0; i < MIXER_CHANNELS; ++i)
		ch[i].data = 0;
	mus.on = 0;
}

void mixer_command(const MixCmd *c)
{
	switch (c->op)
	{
	case MIX_STOP_ALL:
		for (int i = 0; i < MIXER_CHANNELS; ++i)
			ch[i].data = 0;
		mus.on = 0;
		return;
	case MIX_MUSIC_START: {
			const uint8_t *m = c->data;
			const volatile MixInstruments *ins = UNCACHED(c->ext);
			for (int i = 0; i < 15; ++i)
			{
				mus.insData[i] = ins->data[i];
				mus.insVolume[i] = ins->volume[i];
			}
			mus.numOrder = m[0x3F];
			mus.orderTable = m + 0x40;
			mus.patterns = m + 0xC0;
			mus.curOrder = c->vol;
			mus.curPos = 0;              /* SfxPlayer::start */
			mus.delay = c->freq;
			mus.samplesLeft = 0;         /* SfxPlayer::play */
			for (int i = 0; i < 4; ++i)
				mus.mc[i].len = 0;
			mus.on = 1;
		}
		return;
	case MIX_MUSIC_DELAY:
		mus.delay = c->freq;
		return;
	case MIX_MUSIC_STOP:
		mus.on = 0;
		return;
	}
	if (c->chan >= MIXER_CHANNELS)
		return;
	Channel *k = &ch[c->chan];
	switch (c->op)
	{
	case MIX_PLAY: {
			const uint8_t *d = c->data;
			const uint32_t len = (uint32_t)be16(d) * 2;
			k->loopLen = (uint32_t)be16(d + 2) * 2;
			k->loopPos = k->loopLen ? len : 0;
			k->len = len;
			k->pos = 0;
			k->frac = 0;
			k->inc = ((uint32_t)c->freq << 16) / MIXER_RATE;
			k->volume = c->vol;
			k->data = d + 8;
		}
		break;
	case MIX_STOP:
		k->data = 0;
		break;
	case MIX_VOLUME:
		k->volume = c->vol;
		break;
	}
}

/* ------------------------------------------------------------------ mixing */
void mixer_render(int16_t *out, int n)
{
	/* 1. effects, as rawgl Mixer_impl::mixChannels: left, copied to right */
	for (int s = 0; s < n; ++s)
	{
		int sample = 0;
		for (int i = 0; i < MIXER_CHANNELS; ++i)
		{
			Channel *k = &ch[i];
			if (!k->data)
				continue;
			uint32_t pos = k->pos;
			k->frac += k->inc;
			k->pos += k->frac >> 16;
			k->frac &= 0xFFFF;
			if (k->loopLen != 0)
			{
				if (pos >= k->loopPos + k->loopLen)
				{
					pos = k->loopPos;
					k->pos = k->loopPos + (k->inc >> 16);
					k->frac = k->inc & 0xFFFF;
				}
			}
			else if (pos >= k->len)
			{
				k->data = 0;
				continue;
			}
			const int a = k->data[pos] ^ 0x80;
			sample = clip16(sample + (((a << 8) | a) - 32768) * k->volume / 64);
		}
		out[2 * s] = out[2 * s + 1] = (int16_t)sample;
	}
	/* 2. music, as SfxPlayer::readSamples / mixSamples (only while it has a tick delay) */
	if (!mus.on || mus.delay == 0)
		return;
	int16_t *buf = out;
	int len = n;
	while (len != 0)
	{
		if (mus.samplesLeft == 0)
		{
			music_events();
			mus.samplesLeft = MIXER_RATE * (mus.delay * 60 * 1000 / PAULA_FREQ) / 1000;
			if (mus.samplesLeft <= 0)
				mus.samplesLeft = 1;   /* rawgl would loop forever here; never with the game's delays */
		}
		int count = mus.samplesLeft;
		if (count > len)
			count = len;
		mus.samplesLeft -= count;
		len -= count;
		for (int i = 0; i < count; ++i)
		{
			music_mix(buf, &mus.mc[0]);
			music_mix(buf, &mus.mc[3]);
			++buf;
			music_mix(buf, &mus.mc[1]);
			music_mix(buf, &mus.mc[2]);
			++buf;
		}
	}
}
