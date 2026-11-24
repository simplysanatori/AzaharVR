/* PokeDaisy's 3DS-only back ends are never ours: armGBA and gpSP never own a core here, so every probe says
 * "not this core" and the write/state paths fail. These are the only symbols PokeDaisy needs that its own
 * files do not define. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "3ds/emu/pd_armgba.h"
#include "3ds/emu/pd_gpsp.h"
#include "3ds/host/pd_mixer.h"

bool pd_armgba_is(const struct mCore* c) { (void) c; return false; }
bool pd_gpsp_is(const struct mCore* c) { (void) c; return false; }
bool pd_armgba_fast_flash_saves(long* a, long* b) { (void) a; (void) b; return false; }
void pd_armgba_skip_video(bool s) { (void) s; }
bool pd_armgba_write(uint32_t a, const void* d, size_t n) { (void) a; (void) d; (void) n; return false; }
bool pd_armgba_rom_write(uint32_t a, const void* d, size_t n) { (void) a; (void) d; (void) n; return false; }
bool pd_armgba_save_state(const char* p) { (void) p; return false; }
enum pd_armgba_state pd_armgba_load_state(const char* p) { (void) p; return PD_ARMGBA_STATE_BAD; }
void pd_gpsp_skip_video(bool s) { (void) s; }
void pd_gpsp_draw_late(void) {}
bool pd_gpsp_collect(void) { return false; }
bool pd_gpsp_write(uint32_t a, const void* d, size_t n) { (void) a; (void) d; (void) n; return false; }
bool pd_gpsp_rom_write(uint32_t a, const void* d, size_t n) { (void) a; (void) d; (void) n; return false; }
bool pd_gpsp_save_state(const char* p) { (void) p; return false; }
enum pd_gpsp_state pd_gpsp_load_state(const char* p) { (void) p; return PD_GPSP_STATE_BAD; }

/* pd_session_start always attaches the mixer, which plants a breakpoint over the game's own mixer in work
 * RAM (3DS speed hack) that our states and mapcap would copy. pd_mixer.c is left out; attach says no. */
unsigned pd_mixer_mixes;
bool pd_mixer_attach(struct mCore* c) { (void) c; return false; }
void pd_mixer_frame(struct mCore* c) { (void) c; }
void pd_mixer_release(struct mCore* c) { (void) c; }
