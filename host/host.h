/* RAWGL32X host harness - shared declarations */
#ifndef HOST_H__
#define HOST_H__
#include <stdint.h>
struct SystemStub;
SystemStub *SystemStub_Host_create();
void host_mixAudio(uint32_t ms);          /* mixer_host.cpp: mix ms of audio into the WAV */
void host_onFrame(uint32_t frame, uint32_t ms);  /* main_host.cpp: per shown frame (memory log) */
void host_logf(const char *fmt, ...);
uint32_t host_now();                      /* virtual clock (ms) */
bool host_writePNG(const char *path, const uint16_t *rgb555, int w, int h);
struct PadEvent;
int host_parsePad(const char *s, PadEvent *ev, int maxEv);
#endif
