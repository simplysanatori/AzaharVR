/* gba-ds's game.c (unmodified) as a loop the host runs a frame at a time: gbads_game.c. */
#ifndef GBADS_GAME_HOST_H
#define GBADS_GAME_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Emu;

/* What the loop asks of the host, always on the thread that runs it. False: leave now, the call
 * does not come back to game.c. */
typedef struct GbadsHost {
    void* ctx;
    struct Emu* emu;      /* the host's open game: game.c's own `emu` is this one */
    const char* rom_path; /* must outlive the run */
    /* The top of a pass, the first one once the set-up is done. False: the loop ends by itself. */
    bool (*events)(void* ctx, bool* turn_page);
    uint32_t (*keys)(void* ctx);
    /* The game's one frame. A frame game.c runs to draw a picture again is not asked for here. */
    bool (*step)(void* ctx, uint32_t keys);
    /* The pictures are drawn. */
    bool (*present)(void* ctx);
    /* False: leave now, from wherever the loop is. Asked often, must be cheap. */
    bool (*alive)(void* ctx);
    /* profile_set_rom, which others may be reading the profiles around. */
    void (*set_rom)(void* ctx, const char* code, uint32_t crc);
    /* From any thread of game.c's. */
    void (*log)(void* ctx, const char* text);
} GbadsHost;

typedef struct GbadsPicture {
    const uint32_t* pixels; /* XBGR8888, alpha not set, rows `width` long; NULL: none */
    int width, height;
    int shade; /* 0..255: black laid over it */
} GbadsPicture;

/* One session at a time in a process: game.c's state is all static. */
typedef struct GbadsLoop {
    /* The whole session on the calling thread. It puts back what it did to the core. */
    void (*run)(const GbadsHost* host);

    /* Between passes, while the loop's thread waits in one of the calls above: */

    /* Whether the game has a second screen, and its size before there is a picture. */
    bool (*output)(int* width, int* height);
    /* [0] the game's screen, [1] the other: what the last pass drew. */
    void (*pictures)(GbadsPicture out[2]);
    /* game.c's own Restart, the core's reset aside: that is the host's. */
    void (*restart)(void);
    /* What game.c does after it loads a state. */
    void (*state_loaded)(void);
} GbadsLoop;

extern const GbadsLoop gbads_game_loop;

#ifdef __cplusplus
}
#endif

#endif
