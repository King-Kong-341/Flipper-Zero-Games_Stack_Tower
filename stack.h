/*
 * Stack Tower for the Flipper Zero
 * --------------------------------
 * A faithful remake of the classic "Stack" arcade game in isometric 3D:
 * a slab slides back and forth over the tower, press OK to drop it.
 * Whatever hangs over the edge is sliced off, land it exactly for a
 * "perfect" - eight perfects in a row make the slab grow back.
 *
 * Module overview:
 *   stack_main.c    - app setup, main loop, screen switching, input routing
 *   stack_game.c    - game rules, physics, camera, demo autopilot
 *   stack_render.c  - 1-bit framebuffer, isometric 3D renderer, backgrounds
 *   stack_screens.c - intro, title, game HUD, pause, game over, menus
 *   stack_fx.c      - sound sequencer, vibration and RGB LED effects
 *   stack_store.c   - settings + statistics on the SD card
 */
#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TAG "StackTower"
#define APP_VERSION "1.0"

/* ---------- Timing ---------- */

#define FRAME_MS 25 /* 40 fps */
#define FADE_MS 220 /* screen transition fade */

/* ---------- World ----------
 * x/z are measured in iso grid units (1 unit = 2 px across, 1 px down at
 * zoom 1), y in pixels at zoom 1. The base slab is 11 x 11 units, which
 * gives a 44 x 22 px top face with perfectly clean 2:1 edges. */

#define BLOCK_S 11.0f /* base slab size (grid units) */
#define LAYER_TH 4.0f /* slab thickness (px at zoom 1) */
#define AMP 16.5f /* slide distance from the centre (grid units) */
#define SPEED_START 22.0f /* slide speed at score 0 (grid units / s) */
#define SPEED_STEP 0.30f /* speed gain per placed slab */
#define SPEED_MAX 40.0f
#define PERFECT_TOL 0.8f /* max. miss that still counts as perfect */
#define MIN_SIZE 0.3f /* anything thinner is a miss */
#define GROW_COMBO 8 /* perfects in a row before the slab grows */
#define GROW_AMOUNT 1.0f /* growth per axis (grid units) */
#define GRAVITY 380.0f /* falling pieces (px / s^2) */
#define PILLAR_VIS 10.0f /* pillar part shown in the game-over zoom */

#define RING_MS 480 /* perfect ring effect */
#define GROW_MS 180 /* grow animation */
#define POP_MS 140 /* score "pop" */
#define FALL_MS 800 /* miss -> zoom out */
#define ZOOM_MS 1200 /* game-over camera zoom */
#define PANEL_MS 260 /* game-over panel slide in */
#define COLLAPSE_MS 1100 /* demo tower collapse */
#define DEMO_WAIT_MS 380 /* autopilot "reaction" time */

#define MAIN_CAP 1000
#define DEMO_CAP 16
#define HELP_CAP 14
#define MAX_PIECES 20
#define MAX_RINGS 4

/* ---------- Types ---------- */

typedef struct {
    float x0, x1, z0, z1;
} Slab;

typedef struct {
    float ox, oy, zoom, camy;
} Cam;

typedef struct {
    Slab s;
    float y; /* bottom (world px) */
    float vy; /* downward speed */
    float vd; /* sideways drift along the axis */
    uint8_t axis;
    bool front;
    bool active;
} Piece;

typedef struct {
    Slab s;
    float y;
    uint32_t age;
    bool active;
} Ring;

typedef enum {
    GsPlaying,
    GsFalling, /* missed slab tumbles down */
    GsZoom, /* camera zooms out to show the tower */
    GsOver, /* result panel */
    GsCollapse, /* demo only: tower crumbles, then restarts */
} GamePhase;

typedef enum {
    DemoNone,
    DemoTitle,
    DemoHelpDrop,
    DemoHelpSlice,
    DemoHelpPerfect,
} DemoMode;

#define EV_PLACE (1 << 0)
#define EV_PERFECT (1 << 1)
#define EV_GROW (1 << 2)
#define EV_MISS (1 << 3)

typedef struct {
    Slab* layers; /* layers[0] = top of the base pillar */
    uint16_t count;
    uint16_t cap;
    uint32_t first_index; /* layers dropped from memory in marathon games */

    Slab cur; /* moving slab (rest position) */
    uint8_t axis; /* 0 = slides along x, 1 = along z */
    float off; /* offset from the rest position */
    float dir;
    float speed;
    bool moving;

    uint16_t score;
    uint16_t combo;
    uint16_t max_combo;
    uint16_t perfects;

    GamePhase phase;
    uint32_t phase_t;
    uint32_t since_spawn;

    Piece pieces[MAX_PIECES];
    Ring rings[MAX_RINGS];

    bool growing;
    uint32_t grow_t;
    Slab grow_from;

    uint32_t pop_t;

    Cam cam; /* current camera */
    Cam base; /* camera layout while playing */
    Cam cam0, cam1; /* zoom animation */

    DemoMode demo;
    float demo_err;
    uint32_t rng;

    uint8_t events; /* EV_* flags, consumed by the app for sound/LED */
} Game;

typedef enum {
    ScrIntro,
    ScrTitle,
    ScrGame,
    ScrSettings,
    ScrStats,
    ScrHelp,
} ScreenId;

typedef enum {
    ThemeDay,
    ThemeDots,
    ThemeNight,
    ThemeCount,
} Theme;

typedef enum {
    VolLow,
    VolMid,
    VolHigh,
    VolCount,
} Volume;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t sound;
    uint8_t volume;
    uint8_t vibro;
    uint8_t led;
    uint8_t theme;
    uint8_t intro;
    uint8_t reserved;
    uint32_t best;
    uint32_t games;
    uint32_t blocks;
    uint32_t perfects;
    uint32_t best_streak;
} SaveData;

typedef struct {
    uint16_t f; /* Hz, 0 = rest */
    uint16_t ms;
    uint8_t vol; /* percent of the chosen volume */
} Tone;

typedef struct {
    uint8_t r, g, b;
    uint16_t ms;
    bool fade;
} LedStep;

#define FX_MAX_TONES 16
#define FX_MAX_VIB 12
#define FX_MAX_LED 8

typedef struct {
    Tone tones[FX_MAX_TONES];
    uint8_t tone_n, tone_i;
    uint32_t tone_end;
    bool tone_busy;
    bool speaker;
    uint32_t speaker_retry;

    uint16_t vib[FX_MAX_VIB]; /* on, off, on, off ... (ms) */
    uint8_t vib_n, vib_i;
    uint32_t vib_end;
    bool vib_busy;

    LedStep led[FX_MAX_LED];
    uint8_t led_n, led_i;
    uint32_t led_start;
    bool led_busy;
} Fx;

typedef struct {
    FuriMutex* mutex;
    FuriMessageQueue* queue;
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* notif;
    Storage* storage;
    bool running;
    uint32_t now;

    ScreenId screen;
    uint32_t screen_t;
    uint32_t fade_t;
    bool fading;

    SaveData save;
    Fx fx;

    Game game; /* the real game */
    Game demo; /* title screen autopilot */
    Game help; /* how-to-play autopilot */

    uint8_t fb[128 * 64 / 8];

    /* menu state */
    uint8_t title_sel;
    bool paused;
    uint8_t pause_sel;
    uint8_t set_sel;
    uint8_t set_top;
    bool confirm;
    uint8_t confirm_sel;
    uint8_t stat_top;
    uint8_t help_page;
    uint8_t intro_fired;

    /* game-over state */
    bool new_best;
    bool over_fx_done;
} App;

/* ---------- stack_game.c ---------- */

void game_init(Game* g, uint16_t cap, DemoMode demo);
void game_free(Game* g);
void game_reset(Game* g, const Cam* layout);
void game_update(Game* g, uint32_t dt);
void game_drop(Game* g);
void game_skip_zoom(Game* g);
float game_top_y(const Game* g);
void game_build_intro(Game* g, const Cam* layout, uint8_t slabs);
void game_start_moving(Game* g);

/* ---------- stack_render.c ---------- */

void fb_clear(uint8_t* fb, bool black);
void fb_span(uint8_t* fb, int x0, int x1, int y, uint8_t lvl);
void fb_line(uint8_t* fb, int x0, int y0, int x1, int y1, bool black, uint8_t dash);
void fb_rect_pattern(uint8_t* fb, int x, int y, int w, int h, uint8_t lvl);
void render_background(uint8_t* fb, Theme theme, float camy, uint32_t t);
void render_scene(
    uint8_t* fb,
    const Game* g,
    Theme theme,
    bool show_cur,
    int draw_layers,
    const float* yoff,
    float pillar_off);
void render_logo(uint8_t* fb, int x, int y, Theme theme, const float* letter_dy);
void render_blit(Canvas* canvas, const uint8_t* fb);
void render_fade(Canvas* canvas, int x, int y, int w, int h, uint8_t lvl, bool black);

/* ---------- stack_screens.c ---------- */

void screens_start(App* app);
void screens_draw(App* app, Canvas* canvas);
void screens_update(App* app, uint32_t dt);
void screens_input(App* app, const InputEvent* ev);
void screens_enter(App* app, ScreenId s, bool fade);

/* ---------- stack_fx.c ---------- */

void fx_init(App* app);
void fx_deinit(App* app);
void fx_update(App* app);
uint32_t fx_next_due(App* app);
void fx_stop_all(App* app);
void fx_place(App* app);
void fx_perfect(App* app, uint16_t combo);
void fx_grow(App* app, uint16_t combo);
void fx_miss(App* app);
void fx_result(App* app, bool new_best);
void fx_nav(App* app);
void fx_select(App* app);
void fx_back(App* app);
void fx_toggle(App* app, bool on);
void fx_pause(App* app);
void fx_intro_land(App* app, uint8_t k);
void fx_intro_logo(App* app);
void fx_test_vibro(App* app);
void fx_test_led(App* app);
void fx_test_volume(App* app);

/* ---------- stack_store.c ---------- */

void store_defaults(SaveData* s);
void store_load(App* app);
void store_save(App* app);
void store_reset_stats(App* app);
