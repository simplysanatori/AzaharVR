/* gba-ds's logo.c (unmodified): a title screen's logo, made from the player's own ROM by a silent core
 * on a thread of its own. gbads_logo.c. */
#ifndef GBADS_LOGO_HOST_H
#define GBADS_LOGO_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Around a game. The end joins the queue's thread: what it reads (the profiles, the BIOS path) may
 * change only after it. `log` is called from that thread too. */
void gds_logo_begin(void (*log)(void* ctx, const char* text), void* ctx);
void gds_logo_end(void);

/* The logo's file in `dir`, named as ui.c's extracted_path does, there or not. False: the game has no
 * profile, or the name does not fit (a cut one would be another file). */
bool gds_logo_path(const char* dir, const char* code, uint32_t file_crc, char* out, size_t n);
/* Queues the making of `path` in `dir`, which is made. Not if it is there or was tried before. */
void gds_logo_want(const char* rom, const char* code, const char* dir, const char* path);
/* A PNG no larger than the GBA's screen, R G B A bytes, to free(); NULL: not there, or not one. */
uint8_t* gds_logo_read(const char* path, int* w, int* h);

/* logo.c's own, by their names here (gbads_rename.h) */
uint32_t gds_logo_generation(void);
bool gds_logo_busy(const char* gameCode);

/* For the tests: a picture written as a logo is. */
bool gds_logo_write(const char* path, const uint8_t* rgba, int w, int h);

#ifdef __cplusplus
}
#endif

#endif
