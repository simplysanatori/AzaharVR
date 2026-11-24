/* What ui.c, paths.c, cheats.c and the rest of gba-ds's app would give game.c: part of gbads_game.c.
 * About twenty do something; the others are menus and text, which are never reached. */

/* ---------------------------------------------------------------- what ui.c owns */
View views[2];
int nViews = 1;
Screen screens[2];
int nScreens = 2;
bool btnNow[B_COUNT], btnPrev[B_COUNT];
const char* const hotkeyBtnNames[B_COUNT] = { "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "" };

/* ---------------------------------------------------------------- the sink (drawing) */
void ui_clear(void) { memset(H.rec, 0, sizeof(H.rec)); }
void ui_present(void) {
    if (!H.host->present(H.host->ctx)) gds_leave();
}
void ui_update_texture(SDL_Texture* t, const void* px, int pitch) {
    gds_check();
    if (!t || !t->px || !px) return;
    for (int y = 0; y < t->h; ++y) memcpy(t->px + (size_t) y * t->w, (const char*) px + (size_t) y * pitch, (size_t) t->w * 4);
}
void ui_update_texture_plain(SDL_Texture* t, const void* px, int pitch) { ui_update_texture(t, px, pitch); }
/* (draw_game's pictures; the bar's and the menus' come through ui_image) */
void ui_image_pixels(int s, SDL_Texture* const* t, int w, int h, const char* m, int lcd) {
    (void) m, (void) lcd;
    H.rec[s].t = t[0], H.rec[s].w = w, H.rec[s].h = h, H.rec[s].shade = 0;
}
void ui_image(int s, SDL_Texture* const* t, int w, int h, const char* m, const SDL_Rect* a) { (void) s, (void) t, (void) w, (void) h, (void) m, (void) a; }
/* (black over a whole screen darkens it: hud.alt.dimGame; a screen's own background is opaque) */
void ui_fill(int s, SDL_Rect r, SDL_Color c) {
    if (c.a < 255 && !c.r && !c.g && !c.b && r.w >= GBA_W && r.h >= GBA_H) H.rec[s].shade = c.a;
}
void ui_dim(int s, Uint8 a) { H.rec[s].shade = a; }
void ui_title_block(int s, SDL_Rect a, const char* t, int cap, SDL_Color c) { (void) s, (void) a, (void) t, (void) cap, (void) c; }
SDL_Rect ui_image_rect(int s, int w, int h, const char* m) { (void) m, (void) w, (void) h; return screens[s].rect; }

/* ---------------------------------------------------------------- the loop's hooks */
bool ui_events(void) {
    bool turn = false;
    if (!H.host->events(H.host->ctx, &turn)) return false;
    if (turn) {
        cfg->toggle = true; /* (from here on: without it pages only turn by themselves) */
        toggle_press();
    }
    return true;
}
void ui_read_buttons(void) {
    gds_check();
    memcpy(btnPrev, btnNow, sizeof(btnPrev));
    static const struct { uint32_t k; int b; } map[] = { { K_A, B_A }, { K_B, B_B }, { K_SELECT, B_SELECT }, { K_START, B_START }, { K_RIGHT, B_RIGHT }, { K_LEFT, B_LEFT }, { K_UP, B_UP }, { K_DOWN, B_DOWN }, { K_R, B_R }, { K_L, B_L } };
    uint32_t keys = H.host->keys(H.host->ctx);
    memset(btnNow, 0, sizeof(btnNow));
    for (size_t i = 0; i < sizeof(map) / sizeof(*map); ++i) btnNow[map[i].b] = (keys & map[i].k) != 0;
}
bool ui_next_pointer(Pointer* p) { (void) p; return false; }
const char* ui_script_load(void) { return NULL; }
bool ui_scripted(void) { return false; }
bool ui_script_quiet(void) { return false; }
bool ui_render_reset(void) { return false; }
void ui_tick(void) {}
void ui_stick_dpad(bool on) { (void) on; }
void ui_sync_view(int v) { (void) v; }
void ui_present_budget(int f, Uint64 t, double b) { (void) f, (void) t, (void) b; }
void ui_on_background(void (*fn)(void)) { (void) fn; }
void ui_on_foreground(void (*fn)(void)) { (void) fn; }
double ui_display_hz(int v) { (void) v; return 0; }
double ui_last_present_ms(int v) { (void) v; return 0; }
int ui_last_redraws(void) { return 0; }
int ui_volume(void) { return 100; }
void ui_rumble(float l) { (void) l; }
void ui_rumble_pulse(float l, int ms) { (void) l, (void) ms; }
void ui_osd(const char* f, ...) { (void) f; }
void ui_osd_long(const char* f, ...) { (void) f; }
const char* ui_osd_text(void) { return NULL; }
void ui_notice(const char* a, const char* b) { (void) a, (void) b; }
int ui_touch_screen(void) { return 1; }
int ui_other_screen(void) { return 0; }
float ui_px(int s) { (void) s; return 2; }
bool ui_screens_differ(void) { return false; }
void ui_layout_pictures(void) {}
void ui_textures_reset(void) {}
void ui_brightness(int a, int b) { (void) a, (void) b; }
void ui_color_correction(bool on) { (void) on; }
void ui_shader(int s) { (void) s; }

/* logos: none */
SDL_Surface* ui_logo_surface(const Config* c, const char* r, const char* g, bool* p, int* s) { (void) c, (void) r, (void) g, (void) p; if (s) *s = 1; return NULL; }
bool ui_load_logo(const Config* c, const char* r, const char* g, SDL_Texture* t[2], int* w, int* h, bool* p) { (void) c, (void) r, (void) g, (void) w, (void) h, (void) p; t[0] = t[1] = NULL; return false; }
bool ui_logo_provisional(void) { return false; }
bool ui_logo_custom(const Config* c, const char* r, const char* g) { (void) c, (void) r, (void) g; return false; }
const char* ui_logo_note(void) { return ""; }
uint32_t logo_generation(void) { return 0; }

/* ---------------------------------------------------------------- the menus: never opened */
SDL_Color ui_accent_color(int r) { (void) r; return (SDL_Color) { 0, 0, 0, 255 }; }
void ui_clip(int s, const SDL_Rect* c) { (void) s, (void) c; }
void ui_fill_round(int s, SDL_Rect r, SDL_Color c, int x) { (void) s, (void) r, (void) c, (void) x; }
void ui_font_text(int s, int f, int x, int y, int c, const char* t, SDL_Color k, int a) { (void) s, (void) f, (void) x, (void) y, (void) c, (void) t, (void) k, (void) a; }
int ui_font_width(int f, int c, const char* t) { (void) f, (void) c, (void) t; return 1; }
int ui_font_wrap(int s, int f, int x, int y, int w, int c, const char* t, SDL_Color k) { (void) s, (void) f, (void) x, (void) w, (void) c, (void) t, (void) k; return y; }
void ui_free_image(SDL_Texture* t[2]) { if (t[1] && t[1] != t[0]) SDL_DestroyTexture(t[1]); SDL_DestroyTexture(t[0]); t[0] = t[1] = NULL; }
void ui_key(int s, SDL_Rect r, int x, int st) { (void) s, (void) r, (void) x, (void) st; }
SDL_Color ui_key_ink(void) { return (SDL_Color) { 0 }; }
SDL_Color ui_key_mid(int s) { (void) s; return (SDL_Color) { 0 }; }
void ui_key_palette(int s, SDL_Color* a, SDL_Color* b, SDL_Color* c, SDL_Color* d, SDL_Color* e, SDL_Color* f) { (void) s; *a = *b = *c = *d = *e = *f = (SDL_Color) { 0 }; }
void ui_label(int s, int x, int y, int c, const char* t, SDL_Color k) { (void) s, (void) x, (void) y, (void) c, (void) t, (void) k; }
int ui_label_width(int s, int c, const char* t) { (void) s, (void) c, (void) t; return 1; }
bool ui_load_brand(int id, SDL_Texture* o[2], int* w, int* h) { (void) id, (void) w, (void) h; o[0] = o[1] = NULL; return false; }
bool ui_load_image(const char* p, SDL_Texture* o[2], int* w, int* h, bool s) { (void) p, (void) w, (void) h, (void) s; o[0] = o[1] = NULL; return false; }
bool ui_load_surface(SDL_Surface* f, SDL_Texture* o[2], int* w, int* h, bool s) { (void) w, (void) h, (void) s; SDL_FreeSurface(f); o[0] = o[1] = NULL; return false; }
SDL_Surface* ui_resample(const SDL_Surface* s, int w, int h) { (void) s, (void) w, (void) h; return NULL; }
bool ui_screenshot_stacked(const char* p) { (void) p; return false; }
void ui_text(int s, int x, int y, int p, const char* t, SDL_Color c) { (void) s, (void) x, (void) y, (void) p, (void) t, (void) c; }
bool ui_theme_light(void) { return false; }

/* ---------------------------------------------------------------- other modules */
SDL_Surface* bezel_surface(int a, int b, SDL_Rect p, bool ab, uint32_t i, uint32_t o, bool l) { (void) a, (void) b, (void) p, (void) ab, (void) i, (void) o, (void) l; return NULL; }
void hotkeys_resolve(const Config* c, int* mod, int btn[HK_COUNT], bool log) { (void) c, (void) log; *mod = B_HOME; for (int i = 0; i < HK_COUNT; ++i) btn[i] = -1; }
const PixFont pixfont_small, pixfont_large;
int tf_width(int f, int c, const char* t) { (void) f, (void) c, (void) t; return 1; }
bool tf_render(int f, int c, const char* t, TfBitmap* o) { (void) f, (void) c, (void) t; memset(o, 0, sizeof(*o)); return false; }
void tf_free(TfBitmap* b) { (void) b; }
int stbi_write_png(char const* f, int w, int h, int c, const void* d, int s) { (void) f, (void) w, (void) h, (void) c, (void) d, (void) s; return 0; }

/* files: none (the save, the states and the cheats are the host's) */
bool file_exists(const char* p) { (void) p; return false; }
void mkdirs(const char* p) { (void) p; }
void game_paths(const Config* c, const char* rom, GamePaths* o) {
    (void) c, (void) rom;
    memset(o, 0, sizeof(*o));
    strcpy(o->base, "game");
}
void game_paths_patched(const Config* c, GamePaths* o, const PatchChoice* p) { (void) c, (void) o, (void) p; }
int patch_choice_find(const PatchChoice* c, const char* f) { (void) c, (void) f; return -1; }
bool patch_choice_same(const PatchChoice* a, const PatchChoice* b) { (void) a, (void) b; return true; }
void patch_choice_load(const char* p, PatchChoice* c) { (void) p; memset(c, 0, sizeof(*c)); }
void patch_choice_save(const char* p, const PatchChoice* c) { (void) p, (void) c; }
void patch_folder(const Config* c, const char* r, const char* g, char* o, size_t n, bool m) { (void) c, (void) r, (void) g, (void) m; if (n) o[0] = 0; }
int patch_plan(const Config* c, const char* r, const char* g, const PatchChoice* p, char f[][1024], int m) { (void) c, (void) r, (void) g, (void) p, (void) f, (void) m; return 0; }
void patch_title(const char* f, char* o, size_t n) { (void) f; if (n) o[0] = 0; }
/* (cheats_apply would clear the host's from the core they share) */
void cheats_load(Cheats* c, const Config* k, const char* r, const char* g, const char* p) { (void) k, (void) r, (void) g, (void) p; memset(c, 0, sizeof(*c)); }
void cheats_apply(const Cheats* c, Emu* e) { (void) c, (void) e; }
void cheats_save(const Cheats* c) { (void) c; }
int cheats_on(const Cheats* c) { (void) c; return 0; }
