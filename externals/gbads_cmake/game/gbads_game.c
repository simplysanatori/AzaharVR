/* gba-ds's game.c, included unmodified and run as a whole: the host gives it a thread and a frame at
 * a time (gbads_game.h). README.md says what is stubbed. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "gbads_game.h"
#include "gbads_logo.h"
#include "gbads_rename.h"

/* every header of game.c's, before the macros below can touch one */
#include "game.h"
#include "i18n.h"
#include "android.h"
#include "brand.h"
#include "emu.h"
#include "mapcap.h"
#include "hotkeys.h"
#include "paths.h"
#include "ui.h"
#include "bezel.h"
#include "ra.h"
#include "textfont.h"
#include "pixfont.h"
#include "logo.h"
#include "profiles.h"
#include "patch.h"
#include "cheats.h"

/* ---------------------------------------------------------------- the host */
static struct {
    const GbadsHost* host;
    pthread_t thread; /* the loop's */
    jmp_buf out;
    bool live; /* game.c is running: gds_leave has somewhere to go */
    Config c;
    struct { const SDL_Texture* t; int w, h, shade; } rec[2]; /* what draw_game put on each screen */
} H;

/* game.c's `static Emu emu;` becomes `static Emu (*gds_emu);`, this pointer: its one game is the
 * host's, not a copy of it. */
static Emu gds_none;
static Emu* gds_emu = &gds_none;

/* Out of game.c, wherever it is; gds_run_host clears up. It holds no lock at any of these calls. */
static __attribute__((noreturn)) void gds_leave(void) {
    H.live = false;
    longjmp(H.out, 1);
}
static void gds_check(void) {
    if (H.live && pthread_equal(pthread_self(), H.thread) && !H.host->alive(H.host->ctx)) gds_leave();
}
static void gds_texture_gone(const SDL_Texture* t) {
    for (int i = 0; i < 2; ++i)
        if (H.rec[i].t == t) H.rec[i].t = NULL;
}

/* ---------------------------------------------------------------- what game.c calls instead */
/* The game's own frame is run_frame's; any other on the game's core is drawn again and put back by
 * its caller (snapshot_hiding_sprites_poke): no step, and its sound and rumble are nobody's. */
static void gds_frame(const char* from, Emu* e, uint32_t keys) {
    if (e != gds_emu || strcmp(from, "run_frame")) {
        emu_frame(e, keys); /* (the table's copies are on threads of their own) */
        return;
    }
    gds_check();
    if (!H.host->step(H.host->ctx, keys)) gds_leave();
}
static bool gds_open(Emu* e, const char* rom, const char* sav, bool audio) {
    return e == gds_emu || emu_open(e, rom, sav, audio);
}
static void gds_close(Emu* e) {
    if (e != gds_emu) emu_close(e);
}
static void gds_reset(Emu* e) {
    if (e != gds_emu) emu_reset(e);
}
/* Nothing is written to the game but for a frame that is put back. */
static bool gds_may_write(const char* from, const Emu* e) {
    return e != gds_emu || !strcmp(from, "snapshot_hiding_sprites_poke");
}
static void gds_write8(const char* from, Emu* e, uint32_t a, uint8_t v) {
    if (gds_may_write(from, e)) emu_write8(e, a, v);
}
static void gds_write16(const char* from, Emu* e, uint32_t a, uint16_t v) {
    if (gds_may_write(from, e)) emu_write16(e, a, v);
}
static void gds_write32(const char* from, Emu* e, uint32_t a, uint32_t v) {
    if (gds_may_write(from, e)) emu_write32(e, a, v);
}
static void gds_set_rom(const char* code, uint32_t crc) {
    H.host->set_rom(H.host->ctx, code, crc);
}
/* The host paces: the clock takes the sleep instead. */
static int gds_sleep(const struct timespec* t) {
    gds_check();
    gds_clock_ns += (Uint64) t->tv_sec * 1000000000u + (Uint64) t->tv_nsec;
    return 0;
}
static int gds_fprintf(FILE* f, const char* fmt, ...) {
    char line[1024];
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, a);
    va_end(a);
    (void) f; /* (always stderr: game.c opens no file here) */
    size_t l = strlen(line);
    while (l && line[l - 1] == '\n') line[--l] = 0;
    if (l && H.host) H.host->log(H.host->ctx, line);
    return n;
}
static int gds_thread(pthread_t* t, const pthread_attr_t* a, void* (*fn)(void*), void* arg);

#define emu (*gds_emu)
#define emu_frame(e, k) gds_frame(__func__, (e), (k))
#define emu_open(e, r, s, a) gds_open((e), (r), (s), (a))
#define emu_close(e) gds_close(e)
#define emu_reset(e) gds_reset(e)
#define emu_write8(e, a, v) gds_write8(__func__, (e), (a), (v))
#define emu_write16(e, a, v) gds_write16(__func__, (e), (a), (v))
#define emu_write32(e, a, v) gds_write32(__func__, (e), (a), (v))
/* (an empty plan stays, and keeps the patch beside the ROM from every later emu_open of it) */
#define emu_patch_plan(r, f, n) ((void) (r), (void) (f), (void) (n))
#define profile_patch_rom(p, e) ((void) (p), (void) (e), false)
#define profile_set_rom(c, r) gds_set_rom((c), (r))
#define nanosleep(t, r) gds_sleep(t)
#define pthread_create gds_thread
/* no environment, no files */
#define getenv(n) ((char*) 0)
#define unsetenv(n) 0
#define fopen(p, m) ((FILE*) 0)
#define fprintf gds_fprintf
#define fputc(c, f) (c)

#include "game.c"

#undef emu
#undef emu_frame
#undef emu_open
#undef emu_close
#undef emu_reset
#undef emu_write8
#undef emu_write16
#undef emu_write32
#undef emu_patch_plan
#undef profile_patch_rom
#undef profile_set_rom
#undef nanosleep
#undef pthread_create
#undef getenv
#undef unsetenv
#undef fopen
#undef fprintf
#undef fputc

/* The frame-ahead and keep-sound threads would run the game out of turn: refused, game.c then does
 * without. The table's copies get theirs. */
static int gds_thread(pthread_t* t, const pthread_attr_t* a, void* (*fn)(void*), void* arg) {
    if (fn == ahead_worker || fn == keeper_worker) return EAGAIN;
    return pthread_create(t, a, fn, arg);
}

/* gds_frame and gds_may_write go by these two's names: gone, this does not compile. */
static __attribute__((unused)) void gds_names_hold(void) {
    (void) run_frame;
    (void) snapshot_hiding_sprites_poke;
}

#include "gbads_ui_stubs.h"

/* ---------------------------------------------------------------- the session */
static void gds_config(Config* c) {
    config_defaults(c);
    c->overlayMode = OVM_OFF, c->overlays = 0;
    c->shader = SH_OFF;
    c->rewind = c->fastForward = c->audioAuto = c->keepSound = c->pinCores = c->perfMax = false;
    c->autoSave = c->autoLoad = c->barHint = false;
    c->customLogos = CL_OFF, c->logoFolder[0] = 0;
    c->stateDir[0] = c->assetDir[0] = c->cheatDir[0] = c->romDir[0] = c->screenshotDir[0] = c->patchDir[0] = 0;
    strcpy(c->toggleButton, "off");   /* (it would be drawn on the second screen) */
    c->solarLevel = emu_solar_level;  /* (sun_load gives it back) */
    /* the table over both screens takes the top of the game's screen: only where it is all there is */
    const MapProfile* p = profile_find(gds_emu->gameCode);
    c->tallLive = p && p->logoOnly && p->tall.on[0].addr && !p->hud.on[0].addr && !p->snap.showMin;
}

/* What game.c keeps between games and does not set at a game's start. Statics inside its functions
 * (counters of a few frames) stay, as in gba-ds itself. */
static void gds_fresh(void) {
    menuOpen = barOpen = noteOpen = false;
    armedItem = -1;
    aheadReady = aheadInFlight = false;
    wasRewinding = false, rewindBox = 0;
    liveOn = false;
    logoForced = false, logoHolding = 0;
    viewAlt = pressCap = viewWait = capSubject = false;
    capDone = tgCapWant = 0;
    tgState = TG_IDLE, tgFrames = 0, tgTarget = 0, tgWait = -1;
    fadeLevel = 256, fadeUpload = false;
    pageOffset = 0;
    overDirty = overAlone = false;
    combatInGame = false;
    heldN = heldNumW = heldNumH = 0, heldArmed = true;
    cardTaken = cardLogoMade = false, cardPreFor = 0, cardAway = 1000;
    hudSettle = false, hudMapSum = 0;
    screenRestorePending = false;
    memset(pieces, 0, sizeof(pieces));
    mapcap_set_alt(0);
    bug_reset();
}

/* After game_run, or instead of the rest of it: both leave the same. */
static void gds_put_back(void) {
    gds_logo_end(); /* first: its thread reads the profiles, which set_rom below changes */
    keeper_stop();
    ahead_stop();
    mapcap_destroy(mc);
    mc = NULL;
    mapcap_set_alt(0);
    memprobe_virtual = NULL;
    twin_close();
    for (int i = 0; i < MAP_MAX_FRAMES; ++i) free(mapRes.frames[i]);
    memset(&mapRes, 0, sizeof(mapRes));
    free(stillPx), stillPx = NULL, stillValid = false;
    free(combatPx), combatPx = NULL;
    free(screenRestore), screenRestore = NULL, screenRestoreN = 0;
    for (int i = 0; i < BUG_SNAPS; ++i) free(bugSnap[i].st), bugSnap[i].st = NULL, bugSnap[i].have = false;
    ui_free_image(gameTex);
    ui_free_image(mapTex);
    ui_free_image(hudTex);
    ui_free_image(combatTex);
    ui_free_image(logoTex);
    ui_free_image(logoCanvasTex);
    memset(H.rec, 0, sizeof(H.rec));
    /* the game drawn whole again: the host may go on with it */
    if (gds_emu->core) {
        emu_hud_keep_exact(NULL, 0);
        emu_hud_split(gds_emu, false, 0, NULL, 0, hudPx);
        emu_hide_sprites(gds_emu, NULL, 0);
        if (tallProf) emu_hide_exact(gds_emu, NULL, 0); /* (it hooks the renderer even for nothing) */
        emu_hide_grouped(gds_emu, false);
    }
    hudOn = tallOn = false;
    emu_hud_game_reg(0, 0);
    emu_hud_rows(0);
    emu_hud_copy_above(0);
    emu_hud_band(false, false, false);
    emu_hud_by_priority(0);
    emu_hud_keep_bgs(0);
    emu_hud_mem_layers(NULL, NULL, NULL, NULL, 0, false, 0, 0);
    gds_set_rom(NULL, 0);
}

static void gds_run_host(const GbadsHost* host) {
    H.host = host;
    H.thread = pthread_self();
    memset(H.rec, 0, sizeof(H.rec));
    gds_emu = host->emu;
    nViews = 1, nScreens = 2;
    for (int i = 0; i < 2; ++i) screens[i] = (Screen) { .view = 0, .rect = { 0, 0, GBA_W, GBA_H }, .pic = { 0, 0, GBA_W, GBA_H } };
    memset(btnNow, 0, sizeof(btnNow));
    memset(btnPrev, 0, sizeof(btnPrev));
    gds_clock_ns = 1000000000u;
    gds_config(&H.c);
    gds_fresh();
    gds_logo_begin(host->log, host->ctx);
    if (!setjmp(H.out)) {
        H.live = true;
        game_run(&H.c, host->rom_path, false);
        H.live = false;
    }
    gds_put_back();
    gds_emu = &gds_none;
    H.host = NULL;
}

static bool gds_output(int* width, int* height) {
    if (!mc && !hudProf && !snapProf && !tallProf && !pickProf) return false;
    /* (a capture smaller than a screen is a piece of the HUD's) */
    bool hud = hudProf && !hudProf->hud.overMap && !hudProf->hud.drop && (!mc || mapW < GBA_W);
    *width = hud ? HUD_CW : mapW;
    *height = hud ? HUD_CH : mapH;
    return true;
}

static void gds_pictures(GbadsPicture out[2]) {
    for (int i = 0; i < 2; ++i) {
        const SDL_Texture* t = H.rec[i].t;
        bool ok = t && t->px && t->w == H.rec[i].w && t->h == H.rec[i].h;
        out[i] = (GbadsPicture) { ok ? t->px : NULL, ok ? t->w : 0, ok ? t->h : 0, H.rec[i].shade };
    }
}

static void gds_restart(void) {
    /* game.c's Restart item, past its "choose it again" step */
    enum GameExit x;
    armedItem = M_RESTART;
    activate(M_RESTART, &x);
}

static void gds_state_loaded(void) {
    /* what state_load does after a load (copied: it takes a file itself) */
    free(screenRestore);
    screenRestore = NULL, screenRestoreN = 0, screenRestorePending = true;
    bug_reset();
    for (int m = 0; m < 4; ++m) pieces[m].stale = true, pieces[m].still = 0;
    aheadReady = false, mirrorHave = false, hudOffLatch = false;
    /* its callers then set the loop's own forceMap; the end of a rewind does the same */
    wasRewinding = true;
    if (mc) mapcap_cancel(mc); /* (of another moment) */
    /* emu_state_load_file also has the HUD's renderer take on the state as loaded, with a function of
     * emu.c's own: emu_state_load of the very same moment gets there */
    if (hudProf || tallProf) {
        void* st = malloc(emu_state_size(gds_emu));
        if (st && emu_state_save(gds_emu, st)) emu_state_load(gds_emu, st);
        free(st);
    }
}

const GbadsLoop gbads_game_loop = { gds_run_host, gds_output, gds_pictures, gds_restart, gds_state_loaded };
