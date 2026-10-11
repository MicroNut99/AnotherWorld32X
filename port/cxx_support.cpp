/*
 * RAWGL32X - the C++ runtime pieces the engine needs on the SH2 (no libstdc++ is linked):
 * operator new/delete on newlib's malloc, the pure-virtual trap, and the static
 * constructors (the 32X boot does not run them; main_32x.cpp calls rawgl32x_run_ctors()).
 * Built with -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit.
 */
#include <stddef.h>
#include <stdlib.h>
#include "plat.h"

void *operator new(size_t n) {
	void *p = malloc(n ? n : 1);
	if (!p) plat_fatal("OUT OF MEMORY (OPERATOR NEW)");
	return p;
}
void *operator new[](size_t n) { return operator new(n); }
void operator delete(void *p) { free(p); }
void operator delete[](void *p) { free(p); }
void operator delete(void *p, size_t) { free(p); }
void operator delete[](void *p, size_t) { free(p); }

extern "C" void __cxa_pure_virtual() {
	plat_fatal("PURE VIRTUAL FUNCTION CALLED");
}

typedef void (*InitFunc)(void);
// Linker-script symbols, named by their exact assembly names: on the SH2 every C name gets a
// leading '_' (C "x" = assembly "_x"), so a plain extern of __init_array_start would look for
// ___init_array_start (R2 link error). Same trick as plat_mars.c's __rom_program_end.
extern InitFunc init_array_start[] __asm__("__init_array_start");
extern InitFunc init_array_end[]   __asm__("__init_array_end");
extern InitFunc ctors_start[]      __asm__("__ctors_start");
extern InitFunc ctors_end[]        __asm__("__ctors_end");

void rawgl32x_run_ctors() {
	for (InitFunc *f = init_array_start; f < init_array_end; ++f) {
		(*f)();
	}
	// .ctors runs from the end; the list may start with a -1 count and end with 0
	for (InitFunc *f = ctors_end; f > ctors_start; ) {
		--f;
		if (*f != 0 && *f != (InitFunc)-1) (*f)();
	}
}
