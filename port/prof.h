/*
 * Tyrian 32X - frame profiler (only in builds made with PROFILE=1 ./build.sh ...).
 * T32X_PROF(slot): the time since the previous mark was spent in `slot`.
 * The 32X shows each slot's average milliseconds per frame in the top-left corner.
 */
#ifndef T32X_PROF_H
#define T32X_PROF_H

enum
{
	PROF_HUD,      /* status bar, events, everything before the backgrounds */
	PROF_BGFILL,   /* draw_background_1: clearing game_screen (SDL_FillRect) */
	PROF_BG1,      /* draw_background_1: the tiles of layer 1 */
	PROF_BG,       /* background 2 (and the starfield) */
	PROF_BG3,      /* background 3 (taken out of ENEMY and PLAYER) */
	PROF_ENEMIES,  /* enemy logic: movement, aiming, shooting (JE_drawEnemy) */
	PROF_SPRITES,  /* drawing the enemies (blit_enemy) */
	PROF_SHOTS,    /* player shots */
	PROF_PLAYER,   /* player collisions, movement and drawing (and what follows up to the next mark) */
	PROF_ESHOTS,   /* enemy shots: movement, hits on the player, drawing */
	PROF_EXPL,     /* explosions, warnings, everything up to the frame copy */
	PROF_WAIT,     /* waiting for the game's frame timer (idle time) */
	PROF_COPY,     /* playfield copy game_screen -> VGAScreenSeg */
	PROF_PRESENT,  /* plat_present: picture into the frame buffer */
	PROF_OVERLAY,  /* drawing this profiler's own panel (profile builds only) */
	PROF_FLIP,     /* waiting for the screen flip (V-blank) */
	PROF_SLOTS
};

#ifdef T32X_PROFILE
void plat_prof_mark(int slot);
#define T32X_PROF(slot) plat_prof_mark(slot)
#else
#define T32X_PROF(slot) ((void)0)
#endif

#endif
