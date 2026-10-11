/*
 * RAWGL32X - the sound mixer that runs on the slave SH2 (and in the PC test build).
 *
 * Sound effects: Another World's four sound channels - 8-bit samples from the game data
 * (in ROM), each with its own rate, volume 0..63 and an optional loop.
 * Music (R3): the game's own module player (rawgl's SfxPlayer, ported to C below) runs here
 * too, at sample precision, with its own four channels.
 * Output: signed 16-bit stereo at MIXER_RATE, exactly as rawgl's mixer builds it (effects
 * on both sides, music channels 0+3 left and 1+2 right); the 32X PWM gets (L+R)/2 (mono,
 * as Kobo's proven PWM set-up). The PC build's WAV matches rawgl's mixer sample for sample.
 *
 * The music's sync events (the game's VAR_MUSIC_SYNC, used to time the intro) go back to the
 * master through mixer_sync_read(): a sequence number and the last value.
 */
#ifndef AW_MIXER_H
#define AW_MIXER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIXER_CHANNELS 4
#define MIXER_RATE     22050

enum {
	MIX_PLAY = 1, MIX_STOP = 2, MIX_VOLUME = 3, MIX_STOP_ALL = 4,
	MIX_MUSIC_START = 5,   /* data = module, freq = tick delay, vol = start order, ext = instruments */
	MIX_MUSIC_DELAY = 6,   /* freq = new tick delay */
	MIX_MUSIC_STOP = 7
};

/* MIX_MUSIC_START: the 15 instruments, resolved by the master (rawgl's prepareInstruments).
 * The slave reads it through the cache-through alias (it is written just before the command). */
typedef struct
{
	const uint8_t *data[15];   /* sound resource (8-byte header + samples) or 0 */
	uint16_t volume[15];
} MixInstruments;

typedef struct
{
	uint8_t op;            /* MIX_* */
	uint8_t chan;          /* 0..3 */
	uint8_t vol;           /* 0..63; MIX_MUSIC_START: start order */
	uint8_t pad;
	uint16_t freq;         /* MIX_PLAY: sample rate in Hz; MIX_MUSIC_*: tick delay */
	uint16_t pad2;
	const uint8_t *data;   /* MIX_PLAY: the sound resource; MIX_MUSIC_START: the module */
	const MixInstruments *ext;  /* MIX_MUSIC_START */
} MixCmd;

void mixer_init(void);
void mixer_command(const MixCmd *c);
void mixer_render(int16_t *out, int n);   /* n stereo frames: out[2n], left/right */
/* master side: sync sequence number (changes with every sync event) and the last value */
uint32_t mixer_sync_read(int16_t *value);

#ifdef __cplusplus
}
#endif

#endif
