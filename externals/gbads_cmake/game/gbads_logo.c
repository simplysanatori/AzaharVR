/* gba-ds's logo.c, included unmodified, with what a game at a time needs: logo.c itself stops for good.
 * Its core is its own; emu.c's statics it meets are in README.md. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

/* every header of logo.c's, before the macros below can touch one */
#include <mgba/flags.h>
#include <mgba/core/core.h>

#include "gbads_logo.h"
#include "gbads_rename.h"

#include "emu.h"
#include "logo.h"
#include "profiles.h"

/* the length of logo.c's own paths */
enum { GDS_LOGO_PATH = 1024 };

static void (*gds_logo_log)(void* ctx, const char* text);
static void* gds_logo_log_ctx;

static int gds_logo_fprintf(FILE* f, const char* fmt, ...) {
    char line[2048];
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, a);
    va_end(a);
    (void) f; /* (always stderr) */
    size_t l = strlen(line);
    while (l && line[l - 1] == '\n') line[--l] = 0;
    if (l && gds_logo_log) gds_logo_log(gds_logo_log_ctx, line);
    return n;
}

/* stb does not look at what fwrite said, and a cut file would be the game's logo for good. */
static int gds_logo_rename(const char* tmp, const char* out) {
    int w, h;
    uint8_t* px = gds_logo_read(tmp, &w, &h);
    int f = px ? open(tmp, O_RDONLY | O_CLOEXEC) : -1;
    bool whole = f >= 0 && fsync(f) == 0;
    if (f >= 0) close(f);
    free(px);
    if (whole && rename(tmp, out) == 0) return 0;
    unlink(tmp);
    return -1;
}

/* GBADS_SYNC would do the work on the caller's thread, GBADS_LOGO_DEBUG writes more files */
#define getenv(n) ((char*) 0)
#define fprintf gds_logo_fprintf
#define rename(a, b) gds_logo_rename((a), (b))
/* no stbi_ name leaves the object */
#define STB_IMAGE_WRITE_STATIC

#include "logo.c"

#undef getenv
#undef fprintf
#undef rename

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 4096
#include "stb_image.h"

void gds_logo_begin(void (*log)(void* ctx, const char* text), void* ctx) {
    gds_logo_log = log, gds_logo_log_ctx = ctx;
    /* logo_shutdown is for the app's end: it leaves these set, and the job it cut short counts itself
     * off after `pending` was cleared. No thread of logo.c's is alive here. */
    cancelAll = false;
    stopping = false;
    pending = finished = 0;
    logo_pause(true); /* a game is played: only its own logo */
}

void gds_logo_end(void) {
    pthread_mutex_lock(&qlock);
    bool cut = pending > 0;
    pthread_mutex_unlock(&qlock);
    logo_shutdown();
    /* Cut short is not failed: asked again the next time. (A game asks for one logo: the last.) */
    if (cut && nTried) --nTried;
    gds_logo_log = NULL;
}

bool gds_logo_path(const char* dir, const char* code, uint32_t file_crc, char* out, size_t n) {
    if (!n) return false;
    out[0] = 0;
    const MapProfile* p = dir && dir[0] ? profile_find(code) : NULL;
    if (!p) return false;
    int l;
    if (p->logoPerRom && file_crc) l = snprintf(out, n, "%s/%.4s-%08X.png", dir, code, (unsigned) file_crc);
    else if (p->logo.rev) l = snprintf(out, n, "%s/%.4s-r%d.png", dir, code, p->logo.rev);
    else l = snprintf(out, n, "%s/%.4s.png", dir, code);
    if (l > 0 && (size_t) l < n && l < GDS_LOGO_PATH) return true;
    out[0] = 0;
    return false;
}

void gds_logo_want(const char* rom, const char* code, const char* dir, const char* path) {
    if (strlen(rom) >= GDS_LOGO_PATH || strlen(path) >= GDS_LOGO_PATH) return;
    if (exists(path) || (mkdir(dir, 0755) && errno != EEXIST)) return;
    logo_request(rom, code, path);
    logo_front(code, true);
}

uint8_t* gds_logo_read(const char* path, int* w, int* h) {
    int n;
    uint8_t* px = NULL;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (stbi_info_from_file(f, w, h, &n) && *w <= GBA_W && *h <= GBA_H) px = stbi_load_from_file(f, w, h, &n, 4);
    fclose(f);
    return px;
}

bool gds_logo_write(const char* path, const uint8_t* rgba, int w, int h) {
    char tmp[GDS_LOGO_PATH + 8];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int) sizeof(tmp)) return false;
    return stbi_write_png(tmp, w, h, 4, rgba, w * 4) && gds_logo_rename(tmp, path) == 0;
}
