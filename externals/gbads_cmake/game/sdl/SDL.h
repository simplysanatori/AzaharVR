/* The SDL names that gba-ds's game.c and the headers it includes use, for gbads_game.c alone.
 * Every function is static, so no SDL_ symbol leaves the object and the program's real SDL library
 * is never replaced. */
#ifndef GBADS_SDL_STUB_H
#define GBADS_SDL_STUB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t Uint8;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_Point { int x, y; } SDL_Point;
typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef int32_t SDL_JoystickID;
typedef uint32_t SDL_AudioDeviceID;
typedef struct SDL_AudioSpec { int freq; uint16_t format; uint8_t channels; uint16_t samples; } SDL_AudioSpec;
typedef struct SDL_AudioStream SDL_AudioStream;
/* A texture is a plain picture in memory. */
typedef struct SDL_Texture { int w, h; uint32_t* px; } SDL_Texture;
typedef struct SDL_Surface { int w, h, pitch; void* pixels; bool owned; } SDL_Surface;
typedef enum { SDL_POWERSTATE_UNKNOWN, SDL_POWERSTATE_ON_BATTERY, SDL_POWERSTATE_NO_BATTERY } SDL_PowerState;
typedef enum { SDL_ScaleModeNearest, SDL_ScaleModeLinear } SDL_ScaleMode;
typedef enum { SDL_BLENDMODE_NONE, SDL_BLENDMODE_BLEND } SDL_BlendMode;
enum { SDL_TEXTUREACCESS_STREAMING = 1 };
enum { SDL_PIXELFORMAT_ABGR8888 = 1, SDL_PIXELFORMAT_XBGR8888, SDL_PIXELFORMAT_RGBA32 };
#define AUDIO_S16SYS 0x8010

#define SDL_min(a, b) (((a) < (b)) ? (a) : (b))
#define SDL_max(a, b) (((a) > (b)) ? (a) : (b))
#define SDL_clamp(x, a, b) (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x)))
#define SDL_arraysize(a) (sizeof(a) / sizeof((a)[0]))

#define GDS_FN static __attribute__((unused))
static void gds_check(void);
static void gds_texture_gone(const SDL_Texture* t);

/* The clock is ours: it moves only when the loop sleeps, which it does once a frame. */
static Uint64 gds_clock_ns;
GDS_FN Uint64 SDL_GetPerformanceCounter(void) { gds_check(); return gds_clock_ns; }
GDS_FN Uint64 SDL_GetPerformanceFrequency(void) { return 1000000000u; }
GDS_FN Uint32 SDL_GetTicks(void) { gds_check(); return (Uint32) (gds_clock_ns / 1000000u); }
GDS_FN void SDL_Delay(Uint32 ms) { gds_check(); gds_clock_ns += (Uint64) ms * 1000000u; }
GDS_FN const char* SDL_GetError(void) { return ""; }
GDS_FN const char* SDL_GetCurrentVideoDriver(void) { return "none"; }
GDS_FN const char* SDL_GetCurrentAudioDriver(void) { return NULL; }
GDS_FN SDL_PowerState SDL_GetPowerInfo(int* s, int* p) { if (s) *s = -1; if (p) *p = -1; return SDL_POWERSTATE_UNKNOWN; }
GDS_FN bool SDL_PointInRect(const SDL_Point* p, const SDL_Rect* r) { return p->x >= r->x && p->x < r->x + r->w && p->y >= r->y && p->y < r->y + r->h; }

/* No sound device: the host takes the core's sound before the loop sees it. */
GDS_FN int SDL_AudioInit(const char* d) { (void) d; return -1; }
GDS_FN void SDL_AudioQuit(void) {}
GDS_FN SDL_AudioDeviceID SDL_OpenAudioDevice(const char* n, int c, const SDL_AudioSpec* w, SDL_AudioSpec* h, int a) { (void) n, (void) c, (void) a; if (h) *h = *w; return 0; }
GDS_FN void SDL_CloseAudioDevice(SDL_AudioDeviceID d) { (void) d; }
GDS_FN void SDL_PauseAudioDevice(SDL_AudioDeviceID d, int p) { (void) d, (void) p; }
GDS_FN int SDL_QueueAudio(SDL_AudioDeviceID d, const void* p, Uint32 n) { (void) d, (void) p, (void) n; return 0; }
GDS_FN Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID d) { (void) d; return 0; }
GDS_FN void SDL_ClearQueuedAudio(SDL_AudioDeviceID d) { (void) d; }
GDS_FN SDL_AudioStream* SDL_NewAudioStream(uint16_t a, uint8_t b, int c, uint16_t d, uint8_t e, int f) { (void) a, (void) b, (void) c, (void) d, (void) e, (void) f; return NULL; }
GDS_FN void SDL_FreeAudioStream(SDL_AudioStream* s) { (void) s; }
GDS_FN int SDL_AudioStreamPut(SDL_AudioStream* s, const void* p, int n) { (void) s, (void) p, (void) n; return 0; }
GDS_FN int SDL_AudioStreamGet(SDL_AudioStream* s, void* p, int n) { (void) s, (void) p, (void) n; return 0; }

GDS_FN SDL_Texture* SDL_CreateTexture(SDL_Renderer* r, Uint32 f, int a, int w, int h) {
    (void) r, (void) f, (void) a;
    SDL_Texture* t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->w = w, t->h = h;
    t->px = calloc((size_t) w * h, 4);
    return t;
}
GDS_FN void SDL_DestroyTexture(SDL_Texture* t) { if (t) { gds_texture_gone(t); free(t->px); free(t); } }
GDS_FN int SDL_SetTextureBlendMode(SDL_Texture* t, SDL_BlendMode m) { (void) t, (void) m; return 0; }
GDS_FN int SDL_SetTextureScaleMode(SDL_Texture* t, SDL_ScaleMode m) { (void) t, (void) m; return 0; }
GDS_FN int SDL_SetTextureAlphaMod(SDL_Texture* t, Uint8 a) { (void) t, (void) a; return 0; }
GDS_FN SDL_Surface* SDL_CreateRGBSurfaceWithFormatFrom(void* px, int w, int h, int d, int pitch, Uint32 f) {
    (void) d, (void) f;
    SDL_Surface* s = calloc(1, sizeof(*s));
    if (s) s->w = w, s->h = h, s->pitch = pitch, s->pixels = px;
    return s;
}
GDS_FN SDL_Surface* SDL_CreateRGBSurfaceWithFormat(Uint32 fl, int w, int h, int d, Uint32 f) {
    (void) fl;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(calloc((size_t) w * h, 4), w, h, d, w * 4, f);
    if (s) s->owned = true;
    return s;
}
GDS_FN void SDL_FreeSurface(SDL_Surface* s) { if (s) { if (s->owned) free(s->pixels); free(s); } }
GDS_FN int SDL_LockSurface(SDL_Surface* s) { (void) s; return 0; }
GDS_FN void SDL_UnlockSurface(SDL_Surface* s) { (void) s; }
GDS_FN int SDL_SaveBMP(SDL_Surface* s, const char* p) { (void) s, (void) p; return -1; }
#endif
