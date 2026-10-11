/*
 * Another World 32X (RAWGL32X) - platform layer for the real 32X (master SH2).
 * Taken from Tyrian 32X (T3i). Changes, each marked RAWGL32X:
 *   - sound commands carry Another World's fields (rate, sound pointer) - aw_mixer.h
 *   - plat_present_4bpp(): a 4-bit page expanded into the frame buffer
 *   - the data pack is found by its "AWPK" mark; names on the screens
 *
 * Tyrian 32X - platform layer for the real 32X (master SH2).
 *
 * Talks to Kobo's 68000 cartridge program (cart/cart_md.s, unchanged):
 *   start-up  SH2 writes COMM10 = 0x0AAA, the 68000 answers 0x0BBB
 *   CMD 48    pad: COMM0 = 48 -> COMM8 = pad bits, COMM10 = 68000 V-blank count
 *   CMD 52    Genesis text layer: COMM2 = 0 off / 1 on
 * Rules kept from Kobo (proven on hardware): pause between COMM0 polls;
 * every command waits with a timeout.
 *
 * Display: 256-colour packed-pixel mode, double-buffered. Each frame writes
 * a line table + the 320x200 picture into the back buffer, flips at
 * V-blank, then loads CRAM while still in V-blank.
 *
 * Clock: the SH2 watchdog timer in interval mode. crt0.s re-arms it at
 * Pphi/4096 on every overflow and counts overflows in mars_pwdt_ovf_count,
 * so time = (overflows * 256 + WTCNT) * 4096 / Pphi. (The FRT can't be used:
 * crt0.s keeps resetting it for its interrupt-level trick.)
 */
#include "plat.h"

#include <malloc.h>
#include <stdint.h>
#include <string.h>

#include "mars/font3x5.h"
#include "prof.h"

/* ------------------------------------------------------------ registers */
#define MARS_ADAPTER_B   (*(volatile uint8_t  *)0x20004000)
#define MARS_INTMSK      (*(volatile uint16_t *)0x20004000)
#define MARS_CMD_CLR     (*(volatile uint16_t *)0x2000401A)
#define MARS_COMM0       (*(volatile uint16_t *)0x20004020)
#define MARS_COMM2       (*(volatile uint16_t *)0x20004022)
#define MARS_COMM8       (*(volatile uint16_t *)0x20004028)
#define MARS_COMM10      (*(volatile uint16_t *)0x2000402A)
#define MARS_COMM12      (*(volatile uint16_t *)0x2000402C)
#define VDP_DISPMODE     (*(volatile uint16_t *)0x20004100)
#define VDP_FBCTL        (*(volatile uint16_t *)0x2000410A)
#define VDP_CRAM         ((volatile uint16_t *)0x20004200)
#define FB16             ((volatile uint16_t *)0x24000000)
#define FB32             ((volatile uint32_t *)0x24000000)

#define FBCTL_VBLK       0x8000
#define FBCTL_PEN        0x2000
#define FBCTL_FS         0x0001
#define DISP_MODE_256    0x0001
#define DISP_PRI_TEXT    0x0080   /* set = Genesis text in front (Kobo K18) */

#define SH2_WTCSR_TCNT   (*(volatile uint16_t *)0xFFFFFE80)  /* write: 0xA5xx = WTCSR, 0x5Axx = WTCNT */
#define SH2_WTCNT_R      (*(volatile uint8_t  *)0xFFFFFE81)
#define SH2_IPRA         (*(volatile uint16_t *)0xFFFFFEE2)
#define SH2_VCRWDT       (*(volatile uint16_t *)0xFFFFFEE4)

/* SH2 on-chip DMA controller, channel 0 (SH7604 manual, section 9) */
#define SH2_DMA_SAR0     (*(volatile uint32_t *)0xFFFFFF80)  /* source address */
#define SH2_DMA_DAR0     (*(volatile uint32_t *)0xFFFFFF84)  /* destination address */
#define SH2_DMA_TCR0     (*(volatile uint32_t *)0xFFFFFF88)  /* transfer count (units) */
#define SH2_DMA_CHCR0    (*(volatile uint32_t *)0xFFFFFF8C)  /* channel control */
#define SH2_DMA_DRCR0    (*(volatile uint8_t  *)0xFFFFFE71)  /* request source select */
#define SH2_DMA_DMAOR    (*(volatile uint32_t *)0xFFFFFFB0)  /* operation register (all channels) */
/* CHCR: DM=01 destination increments, SM=01 source increments, TS=10 32-bit
 * units, AR=1 auto-request (memory to memory), TB=0 cycle-steal (the CPU gets
 * the bus between transfers), IE=0 no interrupt, DE=1 start. TE (bit 1) is
 * set when the transfer has ended. */
#define CHCR_MEM_TO_MEM_LONG 0x00005A01u
#define CHCR_TE              0x00000002u

#define CMD_GET_PAD      48
#define CMD_TEXT_ONOFF   52

/* Pphi on NTSC: 23.011364 MHz. (PAL: 22.801467 MHz - time would run 0.9 % fast.) */
#define PPHI_HZ          23011364u

/* Frame buffer layout (byte offsets) in each of the two 128 KB buffers:
 *   0x00000  line table (256 words)
 *   0x00200  200 picture lines (64000 bytes)      PLAT_FB_DISPLAY
 *   0x0FE00  one blank line for the 24 border lines
 *   0x10000  64000 bytes free (to 0x1FA00)         PLAT_FB_SPARE  */
#define FB_PIX_WORD      0x100                         /* pixels start at byte 0x200 */
#define FB_LINE_WORDS    (PLAT_SCREEN_W / 2)           /* 160 */
#define FB_BLANK_LINE    PLAT_GAME_H                   /* line 200 */

/* ---------------------------------------------- symbols for crt0.s */
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;
static volatile uint32_t vblank_count = 0;

void pri_vbi_handler(void) { vblank_count++; }
void pri_cmd_handler(void) { MARS_CMD_CLR = 0; }
void sec_cmd_handler(void) { MARS_CMD_CLR = 0; }
void sec_dma1_handler(void) { }

/* ---------------------------------------------- sound: the slave SH2 */
/* The slave SH2 runs the sound effect mixer (port/mixer.c) and feeds the 32X
 * PWM, mono, 22050 Hz (cycle = SH2 clock / 22050). The master hands it
 * commands through a ring in SDRAM, always accessed through the cache-through
 * alias (address | 0x20000000): each SH2 has its own cache, and neither may
 * see a stale copy. The PWM setup follows Kobo's C2b player (control 0x0185,
 * the mono register, FIFO-full bit 15). */
#define PWM_CTRL    (*(volatile uint16_t *)0x20004030)
#define PWM_CYCLE   (*(volatile uint16_t *)0x20004032)
#define PWM_MONO    (*(volatile uint16_t *)0x20004038)
#define PWM_CYC     1044               /* 23.01 MHz / 1044 = 22041 Hz */
#define PWM_MID     (PWM_CYC / 2)
#define SND_RING    16

typedef struct
{
	uint32_t write;              /* commands written by the master */
	uint32_t read;               /* commands taken by the slave */
	MixCmd cmd[SND_RING];
} SndRing;
static SndRing snd_ring_mem;
#define SND_RING_UNCACHED ((volatile SndRing *)((uintptr_t)&snd_ring_mem | 0x20000000u))

void plat_sound_command(const MixCmd *c)
{
	volatile SndRing *r = SND_RING_UNCACHED;
	const uint32_t w = r->write;
	/* the slave takes commands every 16 samples; if the ring is full, wait a
	 * little, then drop the command rather than hang */
	for (uint32_t n = 0; w - r->read >= SND_RING; ++n)
		if (n > 100000u)
			return;
	volatile MixCmd *slot = &r->cmd[w % SND_RING];
	slot->op = c->op;      /* RAWGL32X: Another World's command fields */
	slot->chan = c->chan;
	slot->vol = c->vol;
	slot->freq = c->freq;
	slot->data = c->data;
	slot->ext = c->ext;    /* RAWGL32X R3: music instruments */
	r->write = w + 1;  /* after the fields: both are cache-through, in order */
}

void secondary(void)
{
	volatile SndRing *r = SND_RING_UNCACHED;
	mixer_init();

	PWM_MONO = 1; PWM_MONO = 1; PWM_MONO = 1;  /* prime the FIFO (Kobo) */
	PWM_CYCLE = PWM_CYC;
	PWM_CTRL = 0x0185;

	int16_t block[32];   /* RAWGL32X R3: 16 stereo frames; the PWM gets (L+R)/2 */
	for (;;)
	{
		while (r->read != r->write)
		{
			volatile MixCmd *s = &r->cmd[r->read % SND_RING];
			const MixCmd c = { .op = s->op, .chan = s->chan, .vol = s->vol,
			                   .freq = s->freq, .data = s->data, .ext = s->ext };   /* RAWGL32X */
			mixer_command(&c);
			r->read = r->read + 1;
		}
		mixer_render(block, 16);
		for (int i = 0; i < 16; ++i)
		{
			const int mono = (block[2 * i] + block[2 * i + 1]) >> 1;   /* RAWGL32X R3 */
			int v = PWM_MID + ((mono * PWM_MID) >> 15);
			if (v < 1)
				v = 1;
			if (v > PWM_CYC - 1)
				v = PWM_CYC - 1;
			while (PWM_MONO & 0x8000) { }  /* FIFO full */
			PWM_MONO = (uint16_t)v;
		}
	}
}

/* ------------------------------------------------------------ helpers */
static void set_sr(uint32_t sr) { __asm__ __volatile__("ldc %0,sr" : : "r"(sr) : "memory"); }

/* HW6 (Kobo): never hammer a COMM register the other CPU writes. ~20 us. */
static void comm_pause(void)
{
	volatile int k;
	for (k = 0; k < 64; k++) { }
}

/* Sends a command and waits (with a timeout) for the 68000 to clear COMM0. */
static int send_cmd(uint16_t cmd, uint32_t tries)
{
	MARS_COMM0 = cmd;
	for (uint32_t n = 0; n < tries && MARS_COMM0 != 0; n++)
		comm_pause();
	return MARS_COMM0 == 0;
}

static void take_fb(void)
{
	MARS_ADAPTER_B = 0x80;
	while ((MARS_INTMSK & 0x8000) == 0) { }
}

static void flip_wait(void)
{
	uint16_t cur = VDP_FBCTL & FBCTL_FS;
	VDP_FBCTL = cur ^ FBCTL_FS;
	while ((VDP_FBCTL & FBCTL_FS) == cur) { }
}

static void set_genesis_text(int on)
{
	MARS_COMM2 = on ? 1 : 0;
	send_cmd(CMD_TEXT_ONOFF, 800000UL);
	uint16_t m = VDP_DISPMODE;
	VDP_DISPMODE = on ? (uint16_t)(m | DISP_PRI_TEXT) : (uint16_t)(m & ~DISP_PRI_TEXT);
}

/* RAWGL32X R4: status text in the bottom border: 12 lines of their own in the frame
 * buffer's spare area (byte 0x10000), pointed to by the line table while a text is set. */
static const char *status_text;
#define FB_STATUS_WORD   0x8000                        /* byte 0x10000 */
#define STATUS_FIRST_ROW (PLAT_TOP_BORDER + PLAT_GAME_H)  /* screen row 212 */
#define STATUS_ROWS      (PLAT_SCREEN_H - STATUS_FIRST_ROW)  /* 12 */
#define STATUS_COLOUR    16

void plat_set_status(const char *text) { status_text = text; }

static void draw_status(void)
{
	if (!status_text)
		return;
	volatile uint32_t *z = FB32 + (FB_STATUS_WORD * 2) / 4;
	for (int i = 0; i < STATUS_ROWS * PLAT_SCREEN_W / 4; ++i)
		z[i] = 0;
	volatile uint8_t *pix = (volatile uint8_t *)FB16 + FB_STATUS_WORD * 2;
	int col = 0;
	for (const char *s = status_text; *s && col < PLAT_SCREEN_W / 8 - 1; ++s, ++col)
	{
		int c = (unsigned char)*s;
		if (c >= 'a' && c <= 'z')
			c -= 32;
		if (c < 32 || c > 95)
			c = '?';
		const uint16_t g = font3x5[c - 32];
		for (int gy = 0; gy < 5; ++gy)
			for (int gx = 0; gx < 3; ++gx)
				if (g & (1u << (14 - (gy * 3 + gx))))
				{
					volatile uint8_t *p = pix + (1 + gy * 2) * PLAT_SCREEN_W + 4 + col * 8 + gx * 2;
					p[0] = p[1] = p[PLAT_SCREEN_W] = p[PLAT_SCREEN_W + 1] = STATUS_COLOUR;
				}
	}
}

/* Line table for the game picture: 200 lines centred, borders on the blank line. */
static void write_game_line_table(void)
{
	for (int y = 0; y < 256; ++y)
	{
		int gy = y - PLAT_TOP_BORDER;
		int line = (gy >= 0 && gy < PLAT_GAME_H) ? gy : FB_BLANK_LINE;
		FB16[y] = (uint16_t)(FB_PIX_WORD + line * FB_LINE_WORDS);
		/* RAWGL32X R4: the status lines */
		if (status_text && y >= STATUS_FIRST_ROW && y < PLAT_SCREEN_H)
			FB16[y] = (uint16_t)(FB_STATUS_WORD + (y - STATUS_FIRST_ROW) * FB_LINE_WORDS);
	}
	draw_status();
}

static void clear_back_buffer(void)
{
	/* line table + 201 lines */
	uint32_t longs = (0x200 + (PLAT_GAME_H + 1) * PLAT_SCREEN_W) / 4;
	for (uint32_t i = 0; i < longs; ++i)
		FB32[i] = 0;
}

/* -------------------------------------------------------------- startup */
static void wdt_start(void)
{
	SH2_VCRWDT = (uint16_t)((65 << 8) | (SH2_VCRWDT & 0x00FF));  /* crt0 vector 65 = WDT */
	SH2_IPRA = (uint16_t)((SH2_IPRA & 0xFF0F) | 0x0020);          /* level 2 -> pri_wdt_irq */
	SH2_WTCSR_TCNT = 0x5A00;                                      /* WTCNT = 0 */
	SH2_WTCSR_TCNT = 0xA53E;                                      /* interval mode, on, Pphi/4096 */
}

/* Stack guard: a pattern at the bottom of the master stack (tyrian.ld:
 * 0x0603B800 - 0x0603F7FF), checked at every frame. */
#define STACK_GUARD_ADDR  ((volatile uint32_t *)0x0603B800)
#define STACK_GUARD_WORDS 16
#define STACK_GUARD_VALUE 0x5AA5C33Cu

static void stack_guard_set(void)
{
	for (int i = 0; i < STACK_GUARD_WORDS; ++i)
		STACK_GUARD_ADDR[i] = STACK_GUARD_VALUE ^ (uint32_t)i;
}

static void stack_guard_check(void)
{
	for (int i = 0; i < STACK_GUARD_WORDS; ++i)
		if (STACK_GUARD_ADDR[i] != (STACK_GUARD_VALUE ^ (uint32_t)i))
			plat_fatal("STACK OVERFLOW: A FUNCTION NEEDS MORE THAN THE 16 KB MASTER STACK");
}

int main(int argc, char *argv[]);
void plat_exit(int code) __attribute__((noreturn));

void plat_mars_entry(void)
{
	/* Kobo's start-up sequence (sh2_main.c main), minus its diagnostics. */
	MARS_COMM0 = 0;
	MARS_INTMSK |= 0x0002;
	set_sr(0);
	MARS_COMM10 = 0x0AAA;
	while (MARS_COMM10 != 0x0BBB) { comm_pause(); }

	take_fb();
	wdt_start();
	stack_guard_set();

	VDP_DISPMODE = (uint16_t)((VDP_DISPMODE & 0x8000) | DISP_MODE_256);
	set_genesis_text(0);

	/* Both frame buffers start black with a valid line table. */
	for (int i = 0; i < 2; ++i)
	{
		clear_back_buffer();
		write_game_line_table();
		flip_wait();
	}
	for (int i = 0; i < 256; ++i)
		VDP_CRAM[i] = 0;

	static char arg0[] = "rawgl";  /* RAWGL32X */
	static char *argv[] = { arg0, NULL };
	plat_exit(main(1, argv));
}

void plat_init(void)
{
	/* Hardware is set up in plat_mars_entry(), before main(). */
}

/* ---------------------------------------------------------------- clock */
uint32_t plat_ticks_ms(void)
{
	static uint32_t last_ms;
	uint32_t ovf, cnt;

	do
	{
		ovf = mars_pwdt_ovf_count;
		cnt = SH2_WTCNT_R;
	} while (ovf != mars_pwdt_ovf_count);

	uint32_t ms;
	if (ovf == 0 && vblank_count > 120)
	{
		/* The watchdog never fired: fall back to V-blanks (16.7 ms steps). */
		ms = (uint32_t)((uint64_t)vblank_count * 1000u / 60u);
	}
	else
	{
		uint64_t counts = (uint64_t)ovf * 256u + cnt;
		ms = (uint32_t)(counts * 4096u * 1000u / PPHI_HZ);
	}

	/* An overflow that is pending (interrupts masked) briefly makes the
	 * reading jump back by one period; never let time go backwards. */
	if ((int32_t)(ms - last_ms) < 0)
		ms = last_ms;
	last_ms = ms;
	return ms;
}

/* time() would give nearly the same value at every power-on (it comes from our
 * tick counter): mix the free-running watchdog counter and the V-blank count. */
uint32_t plat_random_seed(void)
{
	uint32_t s = ((uint32_t)mars_pwdt_ovf_count << 8) ^ SH2_WTCNT_R ^ (vblank_count * 2654435761u);
	return s ^ MARS_COMM10;  /* the 68000's V-blank counter from the last pad request */
}

/* ------------------------------------------------------------------ pad */
uint16_t plat_read_pad(void)
{
	static uint16_t pad;
	static uint32_t last_ms = 0xFFFFFFFFu;

	/* The game polls in tight loops; ask the 68000 at most every 4 ms. */
	uint32_t now = plat_ticks_ms();
	if (last_ms != 0xFFFFFFFFu && now - last_ms < 4)
		return pad;
	last_ms = now;

	if (!send_cmd(CMD_GET_PAD, 40000UL))
		return pad;  /* timeout: keep the last state */

	uint16_t bits = MARS_COMM8;
	pad = (bits & 0xF000) == 0xF000 ? 0 : (uint16_t)(bits & 0x0FFF);  /* 0xF000 = no pad */
	return pad;
}

/* ------------------------------------------------------------- CD files */
/* Files on the Sega CD's data track (the level files), through the 68000
 * (cart/cd_files.c): command 60 opens by name, 61 reads up to 4 KB, which the
 * 68000 hands over 12 bytes at a time in COMM4..COMM14 (COMM2 = 1: ready,
 * the SH2 sets it back to 0). Generous limits (~10 s per step: the first
 * access may spin the disc up and seek); a timeout counts as "not found"
 * rather than hanging the game. */
#define CMD_CD_OPEN    60
#define CMD_CD_READ    61
#define CMD_CD_PRESENT 62
#define COMMW(n)    (((volatile uint16_t *)0x20004020)[n])  /* n = 0..7: COMM0..COMM14 */
#define CD_TRIES    1100000UL                                 /* x ~9 us = ~10 s */

/* Only asks the 68000 for its cd_ok flag (set by InitCD at start-up): safe
 * at boot, no disc access. */
bool plat_cd_present(void)
{
	static int present = -1;
	if (present < 0)
	{
		COMMW(2) = 0;
		present = send_cmd(CMD_CD_PRESENT, 40000UL) && COMMW(2) != 0;
	}
	return present != 0;
}

/* A timeout means the 68000 or the Sega CD did not answer: say so on screen
 * (a red error screen) instead of hanging. */
static void cd_fatal(const char *what, const char *name)
{
	static char msg[64];
	char *p = msg;
	for (const char *s = "CD: NO ANSWER ("; *s; )
		*p++ = *s++;
	for (const char *s = what; *s; )
		*p++ = *s++;
	*p++ = ' ';
	for (int i = 0; name[i] && i < 12; ++i)
		*p++ = (name[i] >= 'a' && name[i] <= 'z') ? (char)(name[i] - 32) : name[i];
	*p++ = ')';
	*p = 0;
	plat_fatal(msg);
}

bool plat_cd_open(const char *name, uint32_t *sector, uint32_t *length)
{
	char n[12] = { 0 };
	for (int i = 0; i < 12 && name[i]; ++i)
		n[i] = (name[i] >= 'a' && name[i] <= 'z') ? (char)(name[i] - 32) : name[i];  /* ISO 9660: upper case */
	for (int k = 0; k < 6; ++k)
		COMMW(2 + k) = (uint16_t)(((uint8_t)n[2 * k] << 8) | (uint8_t)n[2 * k + 1]);
	if (!send_cmd(CMD_CD_OPEN, CD_TRIES))
		cd_fatal("OPEN", name);
	const uint32_t len = ((uint32_t)COMMW(2) << 16) | COMMW(3);
	if (len == 0xFFFFFFFFu)
		return false;
	*length = len;
	*sector = ((uint32_t)COMMW(4) << 16) | COMMW(5);
	return true;
}

bool plat_cd_read(uint32_t sector, uint32_t offset, uint32_t bytes, void *dst)
{
	uint8_t *d = dst;
	COMMW(1) = 0;  /* COMM2: no block yet */
	COMMW(2) = (uint16_t)(sector >> 16);
	COMMW(3) = (uint16_t)sector;
	COMMW(4) = (uint16_t)(offset >> 16);
	COMMW(5) = (uint16_t)offset;
	COMMW(6) = (uint16_t)bytes;
	MARS_COMM0 = CMD_CD_READ;

	for (uint32_t got = 0; got < bytes; got += 12)
	{
		uint32_t n = 0;
		while (COMMW(1) != 1)
			if (++n > CD_TRIES)
				cd_fatal("READ", "SECTOR DATA");  /* the 68000 / CD did not deliver */
			else
				comm_pause();
		for (int k = 0; k < 6; ++k)
		{
			const uint16_t w = COMMW(2 + k);
			if (got + 2 * k < bytes)
				d[got + 2 * k] = (uint8_t)(w >> 8);
			if (got + 2 * k + 1 < bytes)
				d[got + 2 * k + 1] = (uint8_t)w;
		}
		COMMW(1) = 0;  /* taken: the 68000 sends the next 12 bytes */
	}
	for (uint32_t n = 0; MARS_COMM0 != 0; ++n)  /* the 68000 finished the command */
		if (n > CD_TRIES)
			return false;
		else
			comm_pause();
	return true;
}

/* ------------------------------------------------------------------ mouse */
#define CMD_MOUSE 63   /* cart/mouse.c: movement summed since the last request */

bool plat_read_mouse(int *dx, int *dy, uint8_t *buttons)
{
	/* the game polls in tight loops: ask the 68000 at most every 4 ms, and
	 * in between report no movement and the same buttons */
	static uint32_t last_ms = 0xFFFFFFFFu;
	static bool last_present;
	static uint8_t last_buttons;
	const uint32_t now = plat_ticks_ms();
	if (last_ms != 0xFFFFFFFFu && now - last_ms < 4)
	{
		*dx = *dy = 0;
		*buttons = last_buttons;
		return last_present;
	}
	last_ms = now;
	last_present = false;
	if (!send_cmd(CMD_MOUSE, 40000UL))
		return false;
	const uint16_t b = COMMW(4);
	if ((b & 0x8000) == 0)
		return false;
	*dx = (int16_t)COMMW(2);
	*dy = (int16_t)COMMW(3);
	*buttons = last_buttons = (uint8_t)(b & 0x0F);
	last_present = true;
	return true;
}

/* --------------------------------------------------------------- network */
/* D32XR's link / serial transport on controller port 2 (cart/net_link.s),
 * through cart_md.s commands 64-67; up to 12 bytes per request in
 * COMM4..COMM14. A link byte waits for the other console's handshake (up to
 * net_link_timeout on the 68000), hence the long limits here. */
#define CMD_NET_SETUP   64
#define CMD_NET_CLEANUP 65
#define CMD_NET_PUT     66
#define CMD_NET_GET     67
#define NET_TRIES       400000UL   /* x ~9 us = ~3.6 s */

bool plat_net_setup(int type)
{
	COMMW(1) = (uint16_t)(type < 0 ? 0x00FF : 0x0001);  /* low byte: negative = serial */
	return send_cmd(CMD_NET_SETUP, NET_TRIES);
}

void plat_net_cleanup(void)
{
	send_cmd(CMD_NET_CLEANUP, NET_TRIES);
}

bool plat_net_send(const uint8_t *bytes, unsigned int count)
{
	bool ok = true;
	while (count > 0)
	{
		const unsigned int n = count < 12 ? count : 12;
		uint8_t b[12] = { 0 };
		for (unsigned int i = 0; i < n; ++i)
			b[i] = bytes[i];
		for (int k = 0; k < 6; ++k)
			COMMW(2 + k) = (uint16_t)((b[2 * k] << 8) | b[2 * k + 1]);
		COMMW(1) = (uint16_t)n;
		if (!send_cmd(CMD_NET_PUT, NET_TRIES) || COMMW(1) != 0)
			ok = false;  /* a byte timed out: the frame is lost, the protocol resends */
		bytes += n;
		count -= n;
	}
	return ok;
}

unsigned int plat_net_recv(uint8_t *bytes, unsigned int most)
{
	unsigned int got = 0;
	while (got < most)
	{
		const unsigned int want = most - got < 12 ? most - got : 12;
		COMMW(1) = (uint16_t)want;
		if (!send_cmd(CMD_NET_GET, NET_TRIES))
			break;
		const unsigned int n = COMMW(1) <= want ? COMMW(1) : 0;
		for (unsigned int i = 0; i < n; ++i)
		{
			const uint16_t w = COMMW(2 + i / 2);
			bytes[got + i] = (uint8_t)((i & 1) ? w : w >> 8);
		}
		got += n;
		if (n < want)
			break;  /* nothing more waiting */
	}
	return got;
}

/* ------------------------------------------------------------------ music */
#define CMD_MUSIC_PLAY 45   /* cart_md.s: CD audio track COMM2, repeating */
#define CMD_MUSIC_STOP 54

void plat_music_play(unsigned int track)
{
	MARS_COMM2 = (uint16_t)track;
	send_cmd(CMD_MUSIC_PLAY, 400000UL);  /* the Sub-CPU accepts it quickly; the seek runs on */
}

void plat_music_stop(void)
{
	send_cmd(CMD_MUSIC_STOP, 400000UL);
}

/* ------------------------------------------------------------------ DMA */
/* Line copies into the frame buffer for layer 1 (port/backgrnd.c), done by
 * the SH2's DMA unit while the CPU composes the next line. Every wait has a
 * limit: if a transfer ever fails to end, the DMA is switched off for good and
 * the caller copies with the CPU (plat_dma_ok). */
static bool dma_ok = true;
static bool dma_busy;
static bool dma_ready;

bool plat_dma_ok(void) { return dma_ok; }

void plat_dma_wait(void)
{
	if (!dma_busy)
		return;
	for (uint32_t n = 0; (SH2_DMA_CHCR0 & CHCR_TE) == 0; ++n)
		if (n > 200000u)
		{
			SH2_DMA_CHCR0 = 0;   /* stop the channel; no more DMA from now on */
			dma_ok = false;
			plat_log("DMA: transfer did not end - switched off (CPU copies)");
			break;
		}
	SH2_DMA_CHCR0 = 0;           /* clears TE (it was read as 1) and DE */
	dma_busy = false;
}

void plat_dma_start(void *dst, const void *src, unsigned int bytes)
{
	plat_dma_wait();
	if (!dma_ok)
	{
		/* fallback: the CPU copies (longwords, both aligned) */
		const uint32_t *s = src;
		volatile uint32_t *d = dst;
		for (unsigned int i = 0; i < bytes / 4; ++i)
			d[i] = s[i];
		return;
	}
	if (!dma_ready)
	{
		SH2_DMA_DMAOR = 0;       /* clear address-error / NMI flags */
		SH2_DMA_DMAOR = 1;       /* DME: DMA on, fixed priority */
		SH2_DMA_DRCR0 = 0;
		dma_ready = true;
	}
	SH2_DMA_CHCR0 = 0;
	SH2_DMA_SAR0 = (uint32_t)src;
	SH2_DMA_DAR0 = (uint32_t)dst;
	SH2_DMA_TCR0 = bytes / 4;
	SH2_DMA_CHCR0 = CHCR_MEM_TO_MEM_LONG;
	dma_busy = true;
}

/* ------------------------------------------------------------ pacing */
/* Even frame pacing. A frame can only appear at a screen refresh (60 per
 * second). A frame needing ~40 ms of work went out at the next refresh after
 * it was done: sometimes 2 refreshes after the previous frame, sometimes 3
 * or 4. The game moves the backgrounds the same distance every frame, so the
 * scrolling looked jerky. Instead, every frame is shown N refreshes after
 * the previous one, N chosen so that 14 of the last 16 frames fit (the two
 * slowest are ignored and come one refresh late). A first version took the
 * slowest of the 16: perfectly even, but one heavy frame held all others
 * back - on the 32X mostly 4-5 refreshes per frame, which felt rougher than
 * the uneven 3-4 before. Gaps over 6 refreshes (loading, menus waiting for
 * input) are not counted.
 * -DT32X_NO_PACING switches it off (flip at the next refresh, as before). */
#define PACE_HISTORY 16
#define PACE_MAX     6
static uint32_t last_flip_vblank;
#ifndef T32X_NO_PACING   /* RAWGL32X: off - the game paces itself (no unused-variable warnings) */
static uint8_t pace_history[PACE_HISTORY];
static unsigned int pace_pos;
static int pace_n = 1;
#endif

static void pace_before_flip(void)
{
#ifndef T32X_NO_PACING
	const uint32_t natural = vblank_count - last_flip_vblank + 1;  /* if flipped now */
	if (natural <= PACE_MAX)
	{
		pace_history[pace_pos++ % PACE_HISTORY] = (uint8_t)natural;
		/* the 3rd largest of the 16: 14 of them fit (insertion into a
		 * small sorted copy, 16 entries) */
		uint8_t sorted[PACE_HISTORY];
		for (int i = 0; i < PACE_HISTORY; ++i)
		{
			int j = i;
			while (j > 0 && sorted[j - 1] > pace_history[i])
			{
				sorted[j] = sorted[j - 1];
				--j;
			}
			sorted[j] = pace_history[i];
		}
		const int n = sorted[PACE_HISTORY - 3];
		pace_n = n > 0 ? n : 1;
		/* the flip happens at the refresh after this wait */
		while (vblank_count - last_flip_vblank < (uint32_t)(pace_n - 1)) { }
	}
	/* Request the flip only outside the vertical blank: the counter ticks at
	 * the start of a blank, and a flip requested during a blank could take
	 * effect in that blank or the next one depending on timing - frames then
	 * came out N-1 or N refreshes apart (a 32X capture showed 3s and 4s mixed
	 * where all should be equal). After the blank, the swap always happens at
	 * the start of the next one. Costs at most the rest of one blank. */
	while (VDP_FBCTL & FBCTL_VBLK) { }
#endif
}

/* ------------------------------------------------------------- profiler */
#ifdef T32X_PROFILE
static uint32_t wdt_counts(void)
{
	uint32_t ovf, cnt;
	do
	{
		ovf = mars_pwdt_ovf_count;
		cnt = SH2_WTCNT_R;
	} while (ovf != mars_pwdt_ovf_count);
	return ovf * 256u + cnt;
}

#define PROF_FRAMES 32
static uint32_t prof_last;
static uint32_t prof_acc[PROF_SLOTS];
/* averages in 1/10 ms; then the total; then free heap (1/10 KB), tiles in the
 * RAM cache and % of layer 1's cells they cover (both shown as n.0) */
#define PROF_LINES (PROF_SLOTS + 8)
static uint16_t prof_tenths[PROF_LINES];
extern int t32x_tile_cache_tiles, t32x_tile_cache_layer3_pct, t32x_tile_cache_layer1_pct;
extern int t32x_layer1_dma;  /* src/backgrnd.c */
static int prof_frames;

void plat_prof_mark(int slot)
{
	uint32_t now = wdt_counts();
	if (slot >= 0 && slot < PROF_SLOTS)
		prof_acc[slot] += now - prof_last;
	prof_last = now;
}

static void prof_frame_done(void)
{
	if (++prof_frames < PROF_FRAMES)
		return;
	uint32_t total = 0;
	for (int i = 0; i <= PROF_SLOTS; ++i)
	{
		uint32_t c = i < PROF_SLOTS ? prof_acc[i] : total;
		if (i < PROF_SLOTS)
			total += c;
		/* 1 watchdog count = 4096 / Pphi seconds */
		prof_tenths[i] = (uint16_t)((uint64_t)c * 4096u * 10000u / PPHI_HZ / PROF_FRAMES);
		if (i < PROF_SLOTS)
			prof_acc[i] = 0;
	}
	prof_tenths[PROF_SLOTS + 1] = (uint16_t)(plat_heap_free() * 10 / 1024);
	prof_tenths[PROF_SLOTS + 2] = (uint16_t)(t32x_tile_cache_tiles * 10);
	prof_tenths[PROF_SLOTS + 3] = (uint16_t)(t32x_tile_cache_layer3_pct * 10);
	prof_tenths[PROF_SLOTS + 4] = (uint16_t)(t32x_tile_cache_layer1_pct * 10);
	prof_tenths[PROF_SLOTS + 5] = (uint16_t)(t32x_layer1_dma * 10);
	prof_tenths[PROF_SLOTS + 6] = (uint16_t)(dma_ok * 10);
	prof_tenths[PROF_SLOTS + 7] = (uint16_t)(pace_n * 10);

	/* layer 1 alternates between DMA and CPU every 256 frames, so one video
	 * measures both (BG1 with L1 DMA 1.0 and 0.0) */
	static unsigned int frames_seen;
	frames_seen += PROF_FRAMES;
	if (frames_seen % 256 == 0)
		t32x_layer1_dma ^= 1;
	prof_frames = 0;
}

/* Text at 1x into the back buffer's picture, over a dark box. */
static void prof_draw(const uint16_t *pal)
{
	static const char *const names[PROF_LINES] = {
		"HUD", "BGFILL", "BG1", "BG2", "BG3", "ENEMY", "SPRITES", "SHOTS", "PLAYER", "ESHOTS", "EXPL", "WAIT", "COPY", "PRESENT", "OVERLAY", "FLIP", "TOTAL",
		"FREE KB", "TILES", "L3 PCT", "L1 PCT", "L1 DMA", "DMA OK", "PACE" };

	/* brightest and darkest palette entries */
	int hi = 0, lo = 0, hv = -1, lv = 1 << 30;
	for (int i = 0; i < 256; ++i)
	{
		int v = (pal[i] & 31) * 2 + ((pal[i] >> 5) & 31) * 4 + ((pal[i] >> 10) & 31);
		if (v > hv) { hv = v; hi = i; }
		if (v < lv) { lv = v; lo = i; }
	}

	volatile uint8_t *pix = (volatile uint8_t *)FB16 + FB_PIX_WORD * 2;
	const int x0 = 2, y0 = 2, w = 13 * 4 + 2, h = PROF_LINES * 6 + 2;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			pix[(y0 + y) * PLAT_SCREEN_W + x0 + x] = (uint8_t)lo;

	for (int line = 0; line < PROF_LINES; ++line)
	{
		char text[16];
		const char *n = names[line];
		int k = 0;
		while (*n)
			text[k++] = *n++;
		while (k < 8)
			text[k++] = ' ';
		unsigned v = prof_tenths[line];
		char num[6];
		int m = 0;
		do { num[m++] = (char)('0' + v % 10); v /= 10; } while (v || m < 2);
		while (m > 1)
			text[k++] = num[--m];
		text[k++] = '.';
		text[k++] = num[0];
		text[k] = '\0';

		for (int c = 0; text[c]; ++c)
		{
			int ch = (unsigned char)text[c];
			uint16_t g = font3x5[(ch < 32 || ch > 95 ? '?' : ch) - 32];
			for (int gy = 0; gy < 5; ++gy)
				for (int gx = 0; gx < 3; ++gx)
					if (g & (1u << (14 - (gy * 3 + gx))))
						pix[(y0 + 1 + line * 6 + gy) * PLAT_SCREEN_W + x0 + 1 + c * 4 + gx] = (uint8_t)hi;
		}
	}
}
#endif

/* -------------------------------------------------------------- display */
/* After the picture is in the back buffer: blank border line, profiler,
 * flip at V-blank, palette. Shared by plat_present and plat_present_hud. */
static void present_finish(volatile uint32_t *blank_line, const uint16_t *palette555)
{
	for (int x = 0; x < PLAT_SCREEN_W / 4; ++x)
		blank_line[x] = 0;

#ifdef T32X_PROFILE
	plat_prof_mark(PROF_PRESENT);
	prof_draw(palette555);
	plat_prof_mark(PROF_OVERLAY);  /* the panel's own cost, not the game's */
#endif
	pace_before_flip();
	flip_wait();
	last_flip_vblank = vblank_count;
#ifdef T32X_PROFILE
	plat_prof_mark(PROF_FLIP);
	prof_frame_done();
#endif

	/* Now in V-blank: load the palette. */
	while ((VDP_FBCTL & FBCTL_PEN) == 0) { }
	for (int i = 0; i < 256; ++i)
		VDP_CRAM[i] = palette555[i];
}

void plat_present_hud(const uint8_t *pixels, int pitch, const uint16_t *palette555)
{
	stack_guard_check();
	write_game_line_table();

	/* the status bar parts only: 14 longwords on the right of rows 0..183,
	 * whole rows 184..199 (both 4-byte aligned in source and destination) */
	volatile uint32_t *pic = FB32 + (FB_PIX_WORD * 2) / 4;
	for (int y = 0; y < PLAT_PLAYFIELD_H; ++y)
	{
		const uint32_t *src = (const uint32_t *)(pixels + y * pitch + PLAT_PLAYFIELD_W);
		volatile uint32_t *dst = pic + (y * PLAT_SCREEN_W + PLAT_PLAYFIELD_W) / 4;
		for (int x = 0; x < (PLAT_SCREEN_W - PLAT_PLAYFIELD_W) / 4; ++x)
			dst[x] = src[x];
	}
	for (int y = PLAT_PLAYFIELD_H; y < PLAT_GAME_H; ++y)
	{
		const uint32_t *src = (const uint32_t *)(pixels + y * pitch);
		volatile uint32_t *dst = pic + (y * PLAT_SCREEN_W) / 4;
		for (int x = 0; x < PLAT_SCREEN_W / 4; ++x)
			dst[x] = src[x];
	}
	present_finish(pic + (PLAT_GAME_H * PLAT_SCREEN_W) / 4, palette555);
}

void plat_present(const uint8_t *pixels, int pitch, const uint16_t *palette555)
{
	stack_guard_check();
	write_game_line_table();

	/* 200 lines into the back buffer. The game's surfaces are 4-byte aligned
	 * with pitch 320, so copy longwords. */
	volatile uint32_t *dst = FB32 + (FB_PIX_WORD * 2) / 4;
	for (int y = 0; y < PLAT_GAME_H; ++y)
	{
		const uint32_t *src = (const uint32_t *)(pixels + y * pitch);
		for (int x = 0; x < PLAT_SCREEN_W / 4; x += 4)
		{
			dst[0] = src[x + 0];
			dst[1] = src[x + 1];
			dst[2] = src[x + 2];
			dst[3] = src[x + 3];
			dst += 4;
		}
	}
	/* blank line for the borders, flip, palette */
	present_finish(dst, palette555);
}

/* RAWGL32X: a 4-bit page (32,000 bytes, two pixels per byte, high nibble left) into the
 * back buffer's picture area. Each byte becomes two frame buffer bytes through a 256-entry
 * table in SDRAM; two source bytes make one 32-bit frame buffer write (16,000 writes). */
void plat_present_4bpp(const uint8_t *page, const uint16_t *palette555)
{
	static uint16_t expand[256];
	if (expand[0x11] == 0)
		for (int b = 0; b < 256; ++b)
			expand[b] = (uint16_t)(((b >> 4) << 8) | (b & 15));

	stack_guard_check();
	write_game_line_table();
	volatile uint32_t *dst = FB32 + (FB_PIX_WORD * 2) / 4;
	const uint16_t *src = (const uint16_t *)page;  /* page is 4-byte aligned (malloc) */
	for (int i = 0; i < PLAT_SCREEN_W * PLAT_GAME_H / 4; ++i)
	{
		const uint16_t two = src[i];
		dst[i] = ((uint32_t)expand[two >> 8] << 16) | expand[two & 0xFF];
	}
	present_finish(dst + PLAT_SCREEN_W * PLAT_GAME_H / 4, palette555);
}

uint8_t *plat_fb_area(int area)
{
	return (uint8_t *)FB16 + (area == PLAT_FB_DISPLAY ? FB_PIX_WORD * 2 : 0x10000);
}

/* ----------------------------------------------------------------- ROM */
extern const uint8_t rom_program_end[] __asm__("__rom_program_end");

const uint8_t *plat_romfs_base(void)
{
	/* tools/make_cart.py puts the data pack on the first 64 KB
	 * boundary after the SH2 program. Check that spot first, then search. */
	uint32_t a = ((uint32_t)rom_program_end + 0xFFFFu) & ~0xFFFFu;
	for (; a < 0x02400000u; a += 0x10000u)
	{
		const uint8_t *p = (const uint8_t *)a;
		if (p[0] == 'A' && p[1] == 'W' && p[2] == 'P' && p[3] == 'K')  /* RAWGL32X: data pack */
			return p;
	}
	return NULL;
}

size_t plat_heap_free(void)
{
	size_t t32x_heap_used(void), t32x_heap_size(void);
	/* the part malloc has not taken from the system yet, plus what was freed
	 * back to malloc (mallinfo().fordblks). The second part was missing at
	 * first: after a level, the freed tile cache counted as used, and the
	 * next level's cache got no tiles at all (32X profiler: TILES 0). Free
	 * memory may be in pieces; the tile cache shrinks its request until one
	 * block fits. */
	return t32x_heap_size() - t32x_heap_used() + (size_t)mallinfo().fordblks;
}

void plat_mem_mark(const char *label)
{
	(void)label;
}

/* ------------------------------------------------------------- messages */
static char last_log[80];

void plat_log(const char *msg)
{
	/* Kept for the error screen; visible in a debugger's memory view too. */
	strncpy(last_log, msg, sizeof last_log - 1);
	last_log[sizeof last_log - 1] = '\0';
	for (char *p = last_log; *p; ++p)
		if (*p == '\n')
			*p = '\0';
}

/* Text screen in the back buffer: 224 lines, 3x5 glyphs drawn 2x (8x12 cells). */
static void text_screen(uint16_t bg, uint16_t fg, const char *const *lines, int count)
{
	for (int y = 0; y < 256; ++y)
		FB16[y] = (uint16_t)(FB_PIX_WORD + (y < PLAT_SCREEN_H ? y : 0) * FB_LINE_WORDS);

	volatile uint8_t *pix = (volatile uint8_t *)FB16 + FB_PIX_WORD * 2;
	for (int i = 0; i < PLAT_SCREEN_W * PLAT_SCREEN_H; ++i)
		pix[i] = 0;

	int row = 1;
	for (int l = 0; l < count && row < PLAT_SCREEN_H / 12; ++l)
	{
		const char *s = lines[l] ? lines[l] : "";
		int col = 1;
		for (; *s && row < PLAT_SCREEN_H / 12; ++s)
		{
			if (*s == '\n' || col >= PLAT_SCREEN_W / 8 - 1)
			{
				row++;
				col = 1;
				if (*s == '\n')
					continue;
			}
			int c = (unsigned char)*s;
			if (c >= 'a' && c <= 'z')
				c -= 32;
			if (c < 32 || c > 95)
				c = '?';
			uint16_t g = font3x5[c - 32];
			for (int gy = 0; gy < 5; ++gy)
				for (int gx = 0; gx < 3; ++gx)
					if (g & (1u << (14 - (gy * 3 + gx))))
					{
						int px = col * 8 + gx * 2, py = row * 12 + gy * 2;
						volatile uint8_t *p = pix + py * PLAT_SCREEN_W + px;
						p[0] = p[1] = p[PLAT_SCREEN_W] = p[PLAT_SCREEN_W + 1] = 1;
					}
			col++;
		}
		row++;
	}

	flip_wait();
	while ((VDP_FBCTL & FBCTL_PEN) == 0) { }
	VDP_CRAM[0] = bg;
	VDP_CRAM[1] = fg;
}

/* Heap figures from port/mars/syscalls.c, for the error screens. */
extern char *t32x_heap_peak;
size_t t32x_heap_used(void);
size_t t32x_heap_size(void);
extern char heap_start_sym[] __asm__("__heap_start");

static void dec(char *out, uint32_t v)
{
	char tmp[11];
	int n = 0;
	do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
	while (n)
		*out++ = tmp[--n];
	*out = '\0';
}

/* "HEAP 123456 USED, PEAK 130000 OF 172000" */
static const char *heap_line(void)
{
	static char line[64];
	char *p = line;
	strcpy(p, "HEAP "); p += 5;
	dec(p, (uint32_t)t32x_heap_used()); p += strlen(p);
	strcpy(p, " USED, PEAK "); p += strlen(p);
	dec(p, t32x_heap_peak ? (uint32_t)(t32x_heap_peak - heap_start_sym) : 0); p += strlen(p);
	strcpy(p, " OF "); p += strlen(p);
	dec(p, (uint32_t)t32x_heap_size());
	return line;
}

static void hex32(char *out, uint32_t v)
{
	for (int i = 7; i >= 0; --i, v >>= 4)
		out[i] = "0123456789ABCDEF"[v & 15];
	out[8] = '\0';
}

/* RAWGL32X R4: credits screen; any of START / A / B / C ends it early. text_screen()
 * draws into the back buffer and flips; the next present redraws the game's line table. */
void plat_text_pause(const char *const *lines, int count, uint32_t ms)
{
	text_screen(0x0000, 0x7FFF, lines, count);
	const uint32_t start = plat_ticks_ms();
	uint16_t prev = 0xFFFF;   /* a button already held at the start does not count */
	while (plat_ticks_ms() - start < ms)
	{
		const uint16_t pad = plat_read_pad() & (PAD_START | PAD_A | PAD_B | PAD_C);
		if (pad & ~prev)
			break;
		prev = pad;
	}
}

void plat_fatal(const char *msg)
{
	set_sr(0xF0);
	const char *lines[] = { "ANOTHER WORLD 32X - FATAL ERROR", "", msg, "", "LAST LOG:", last_log, "", heap_line() };
	text_screen(0x0010 /* dark red */, 0x7FFF, lines, 8);
	for (;;) { }
}

void plat_exception(uint32_t kind, uint32_t pc)
{
	static const char *const kinds[] = { "?", "ILLEGAL INSTRUCTION", "INVALID SLOT INSTRUCTION",
	                                     "ADDRESS ERROR (MISALIGNED ACCESS?)", "DMA ADDRESS ERROR",
	                                     "OTHER EXCEPTION" };
	static char pc_line[24] = "PC = ";
	set_sr(0xF0);
	hex32(pc_line + 5, pc);
	const char *lines[] = { "ANOTHER WORLD 32X - SH2 EXCEPTION", "", kinds[kind < 6 ? kind : 0], pc_line,
	                        "(LOOK THE PC UP IN PORT/MARS/RAWGL.MAP)", "", "LAST LOG:", last_log, "", heap_line() };
	text_screen(0x0010, 0x7FFF, lines, 10);
	for (;;) { }
}

void plat_exit(int code)
{
	set_sr(0xF0);
	const char *lines[] = { "ANOTHER WORLD 32X", "", code == 0 ? "THE GAME HAS ENDED." : "THE GAME STOPPED WITH AN ERROR.",
	                        "", "LAST LOG:", last_log };
	text_screen(0x2800 /* dark blue */, 0x7FFF, lines, 6);
	for (;;) { }
}
