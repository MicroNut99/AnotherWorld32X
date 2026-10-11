/* RAWGL32X - unit test of the pad mapping (port/pad_map.cpp): game and password screen. */
#include <cstdio>
#include <cstring>
#include "../port/pad_map.h"
#include "../port/plat.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL pad mapping: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

int main() {
	PadMapper m;
	PlayerInput pi;
	memset(&pi, 0, sizeof(pi));
	// game: C jumps (= Up), A and B are action, START pauses on the press only
	m.apply(PAD_C | PAD_RIGHT, pi, false);
	CHECK(pi.dirMask == (PlayerInput::DIR_UP | PlayerInput::DIR_RIGHT));
	m.apply(PAD_B, pi, false);
	CHECK(pi.action && pi.dirMask == 0);
	m.apply(PAD_START, pi, false);
	CHECK(pi.pause);
	pi.pause = false;
	m.apply(PAD_START, pi, false);
	CHECK(!pi.pause);                         // held, not pressed again
	m.apply(PAD_MODE, pi, false);
	CHECK(pi.code);
	CHECK(m.helpLine(false) != 0);          // R4b: the notice after MODE
	// R4c: the password screen uses the normal mapping (the game's own letter grid)
	m.apply(0, pi, true);
	m.apply(PAD_RIGHT, pi, true);
	CHECK(pi.dirMask == PlayerInput::DIR_RIGHT && pi.lastChar == 0);
	m.apply(PAD_A, pi, true);
	CHECK(pi.action);
	CHECK(m.helpLine(true) == 0);
	// R4b: A+B+C together asks for the password screen too, and the notice shows
	PadMapper n;
	PlayerInput q;
	memset(&q, 0, sizeof(q));
	n.apply(PAD_A | PAD_B, q, false);
	CHECK(!q.code);
	n.apply(PAD_A | PAD_B | PAD_C, q, false);
	CHECK(q.code);
	CHECK(n.helpLine(false, true) && strstr(n.helpLine(false, true), "PASSWORD SCREEN"));
	CHECK(strstr(n.helpLine(false, false), "NO PASSWORD SCREEN") != 0);
	q.code = false;
	n.apply(PAD_A | PAD_B | PAD_C, q, false);
	CHECK(!q.code);                           // held: no repeat
	if (!fails) printf("PASS pad mapping (game buttons, password letters)\n");
	return fails != 0;
}
