/*
 * All screens: intro animation, title menu, the game itself (HUD, pause,
 * game over), settings, statistics and how-to-play.
 *
 * Layout rules: 128 x 64 px, landscape. The 3D scene is rendered into the
 * framebuffer first, all text/UI is drawn on top with the canvas API.
 * Colours follow the theme: dark ink on light themes, white ink at night.
 */
#include "stack.h"

/* ---------- Camera layouts ---------- */

static const Cam CAM_GAME = {.ox = 64.0f, .oy = 40.0f, .zoom = 1.0f, .camy = 0.0f};
static const Cam CAM_TITLE = {.ox = 97.0f, .oy = 31.0f, .zoom = 0.55f, .camy = 0.0f};
static const Cam CAM_HELP = {.ox = 96.0f, .oy = 33.0f, .zoom = 0.5f, .camy = 0.0f};

/* ---------- Intro timeline (ms) ---------- */

#define IN_PILLAR_END 400
#define IN_SLAB0 400
#define IN_SLAB_GAP 260
#define IN_SLAB_DUR 300
#define IN_SLABS 4
#define IN_LOGO0 1500
#define IN_LETTER_GAP 90
#define IN_LETTER_DUR 300
#define IN_TEXT0 2000
#define IN_TEXT_DUR 300
#define IN_BAR0 2250
#define IN_BAR_DUR 300
#define IN_END 2750

/* ---------- Icons (row bitmaps, bit (w-1) = leftmost pixel) ---------- */

static const uint16_t ICON_GEAR[9] =
    {0x054, 0x0FE, 0x07C, 0x1C7, 0x0C6, 0x1C7, 0x07C, 0x0FE, 0x054};
static const uint16_t ICON_TROPHY[9] =
    {0x1FF, 0x17D, 0x17D, 0x0FE, 0x07C, 0x038, 0x010, 0x038, 0x07C};
static const uint16_t ICON_HELP[9] =
    {0x07C, 0x0C6, 0x006, 0x00C, 0x018, 0x018, 0x000, 0x018, 0x018};
static const uint16_t ICON_PLAY[7] = {0x8, 0xC, 0xE, 0xF, 0xE, 0xC, 0x8};
static const uint16_t ICON_BACK[7] = {0x20, 0x60, 0xFE, 0x61, 0x21, 0x01, 0x1E};

typedef enum {
    TitlePlay,
    TitleSettings,
    TitleStats,
    TitleHelp,
    TitleCount,
} TitleItem;

/* ---------- Small helpers ---------- */

static Theme theme_of(App* app) {
    return (Theme)app->save.theme;
}

static Color fg(App* app) {
    return app->save.theme == ThemeNight ? ColorWhite : ColorBlack;
}

static Color bg(App* app) {
    return app->save.theme == ThemeNight ? ColorBlack : ColorWhite;
}

static float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static float ease_out_cubic(float t) {
    t = clamp01(t);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

static float ease_out_back(float t) {
    t = clamp01(t);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}

static float ease_out_bounce(float t) {
    t = clamp01(t);
    const float n1 = 7.5625f, d1 = 2.75f;
    if(t < 1.0f / d1) return n1 * t * t;
    if(t < 2.0f / d1) {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    }
    if(t < 2.5f / d1) {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}

static void draw_icon(Canvas* c, int x, int y, const uint16_t* rows, int w, int h) {
    for(int r = 0; r < h; r++) {
        for(int col = 0; col < w; col++) {
            if(rows[r] & (1u << (w - 1 - col))) canvas_draw_dot(c, x + col, y + r);
        }
    }
}

/* Text with a 1 px outline in the background colour - stays readable
 * when it overlaps the tower. */
static void halo_text(Canvas* c, int x, int y, Align h, Align v, const char* s, Color f, Color b) {
    canvas_set_color(c, b);
    for(int dy = -1; dy <= 1; dy++) {
        for(int dx = -1; dx <= 1; dx++) {
            if(dx || dy) canvas_draw_str_aligned(c, x + dx, y + dy, h, v, s);
        }
    }
    canvas_set_color(c, f);
    canvas_draw_str_aligned(c, x, y, h, v, s);
}

/* A key cap ("OK") followed by a label. Returns the total width. */
static int key_hint(Canvas* c, int x, int y, const char* key, const char* label, Color f, Color b) {
    canvas_set_font(c, FontSecondary);
    int kw = canvas_string_width(c, key) + 5;
    canvas_set_color(c, f);
    canvas_draw_rbox(c, x, y, kw, 9, 2);
    canvas_set_color(c, b);
    canvas_draw_str_aligned(c, x + kw / 2 + 1, y + 1, AlignCenter, AlignTop, key);
    canvas_set_color(c, f);
    canvas_draw_str_aligned(c, x + kw + 3, y + 1, AlignLeft, AlignTop, label);
    return kw + 3 + canvas_string_width(c, label);
}

static void back_hint(Canvas* c, int x, int y, const char* label, Color f) {
    canvas_set_color(c, f);
    draw_icon(c, x, y + 1, ICON_BACK, 8, 7);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, x + 11, y + 1, AlignLeft, AlignTop, label);
}

static void panel(Canvas* c, int x, int y, int w, int h, Color f, Color b) {
    canvas_set_color(c, b);
    canvas_draw_rbox(c, x, y, w, h, 3);
    canvas_set_color(c, f);
    canvas_draw_rframe(c, x, y, w, h, 3);
}

static void scrollbar(Canvas* c, int x, int y, int h, int pos, int total, int visible, Color f) {
    if(total <= visible) return;
    canvas_set_color(c, f);
    for(int yy = y; yy < y + h; yy += 2) {
        canvas_draw_dot(c, x + 1, yy);
    }
    int th = h * visible / total;
    if(th < 4) th = 4;
    int ty = y + (h - th) * pos / (total - visible);
    canvas_draw_box(c, x, ty, 3, th);
}

/* ---------- Title-screen pieces (shared with the intro) ---------- */

static void draw_title_texts(App* app, Canvas* c, int bar_y, float text_reveal) {
    Color f = fg(app), b = bg(app);
    char buf[24];

    /* "T O W E R" with side rules, under the logo */
    canvas_set_color(c, f);
    canvas_set_font(c, FontSecondary);
    const char* tower = "TOWER";
    int widths[5];
    int total = 0;
    for(int i = 0; i < 5; i++) {
        char ch[2] = {tower[i], 0};
        widths[i] = canvas_string_width(c, ch);
        total += widths[i] + (i < 4 ? 3 : 0);
    }
    int x = 32 - total / 2;
    for(int i = 0; i < 5; i++) {
        char ch[2] = {tower[i], 0};
        canvas_draw_str(c, x, 29, ch);
        x += widths[i] + 3;
    }
    canvas_draw_line(c, 4, 26, 32 - total / 2 - 5, 26);
    canvas_draw_line(c, 32 + total / 2 + 4, 26, 60, 26);

    /* best score */
    snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)app->save.best);
    canvas_set_font(c, FontPrimary);
    int w = canvas_string_width(c, buf) + 12;
    int bx = 32 - w / 2;
    draw_icon(c, bx, 34, ICON_TROPHY, 9, 9);
    canvas_draw_str(c, bx + 12, 43, buf);

    if(text_reveal < 1.0f) {
        render_fade(c, 0, 20, 64, 26, (uint8_t)(16.0f * (1.0f - text_reveal)), b == ColorBlack);
    }

    /* bottom menu bar */
    if(bar_y < 64) {
        canvas_set_color(c, b);
        canvas_draw_box(c, 0, bar_y - 1, 128, 64 - bar_y + 1);
        const struct {
            int x, w;
        } btn[TitleCount] = {{1, 58}, {62, 21}, {85, 21}, {108, 19}};
        for(int i = 0; i < TitleCount; i++) {
            bool sel = app->title_sel == i;
            int bxx = btn[i].x, by = bar_y, bw = btn[i].w, bh = 13;
            canvas_set_color(c, f);
            if(sel) {
                canvas_draw_rbox(c, bxx, by, bw, bh, 3);
            } else {
                canvas_draw_rframe(c, bxx, by, bw, bh, 3);
            }
            canvas_set_color(c, sel ? b : f);
            int cx = bxx + bw / 2;
            if(i == TitlePlay) {
                canvas_set_font(c, FontPrimary);
                int tw = canvas_string_width(c, "PLAY");
                int sx = cx - (tw + 8) / 2;
                draw_icon(c, sx, by + 3, ICON_PLAY, 4, 7);
                canvas_draw_str(c, sx + 8, by + 10, "PLAY");
            } else {
                const uint16_t* ic = i == TitleSettings ? ICON_GEAR :
                                     i == TitleStats    ? ICON_TROPHY :
                                                          ICON_HELP;
                draw_icon(c, cx - 4, by + 2, ic, 9, 9);
            }
        }
    }
}

/* ---------- Intro ---------- */

static void intro_prepare(App* app) {
    game_build_intro(&app->demo, &CAM_TITLE, IN_SLABS);
    app->intro_fired = 0;
}

static void intro_update(App* app) {
    uint32_t t = app->screen_t;
    /* One-shot effects, fired when the timeline passes them. The bounce
     * easing first touches the ground at 1/2.75 of its duration. */
    for(uint8_t k = 0; k < IN_SLABS; k++) {
        uint32_t land = IN_SLAB0 + k * IN_SLAB_GAP + (IN_SLAB_DUR * 4) / 11;
        if(!(app->intro_fired & (1 << k)) && t >= land) {
            app->intro_fired |= (uint8_t)(1 << k);
            fx_intro_land(app, k);
        }
    }
    if(!(app->intro_fired & 0x10) && t >= IN_LOGO0 + 60) {
        app->intro_fired |= 0x10;
        fx_intro_logo(app);
    }
    if(t >= IN_END) {
        game_start_moving(&app->demo);
        screens_enter(app, ScrTitle, false);
    }
}

static void intro_draw(App* app, Canvas* c) {
    uint32_t t = app->screen_t;
    Theme th = theme_of(app);
    Game* g = &app->demo;

    render_background(app->fb, th, g->cam.camy, app->now);

    float pillar = -(1.0f - ease_out_cubic((float)t / IN_PILLAR_END)) * 70.0f;
    float yoff[1 + IN_SLABS] = {0};
    int shown = 1;
    for(int k = 0; k < IN_SLABS; k++) {
        int32_t st = (int32_t)t - (IN_SLAB0 + k * IN_SLAB_GAP);
        if(st < 0) break;
        shown = k + 2;
        yoff[k + 1] = (1.0f - ease_out_bounce((float)st / IN_SLAB_DUR)) * 46.0f;
    }
    render_scene(app->fb, g, th, false, shown, yoff, pillar);

    float dy[5];
    for(int l = 0; l < 5; l++) {
        int32_t st = (int32_t)t - (IN_LOGO0 + l * IN_LETTER_GAP);
        dy[l] = st < 0 ? -40.0f : -24.0f * (1.0f - ease_out_back((float)st / IN_LETTER_DUR));
    }
    render_logo(app->fb, 3, 4, th, dy);
    render_blit(c, app->fb);

    float reveal = clamp01(((float)t - IN_TEXT0) / IN_TEXT_DUR);
    int bar_y = 51 + (int)((1.0f - ease_out_cubic(((float)t - IN_BAR0) / IN_BAR_DUR)) * 14.0f);
    draw_title_texts(app, c, bar_y, reveal);
}

/* ---------- Title ---------- */

static void title_draw(App* app, Canvas* c) {
    Theme th = theme_of(app);
    render_background(app->fb, th, app->demo.cam.camy, app->now);
    render_scene(app->fb, &app->demo, th, true, -1, NULL, 0.0f);
    render_logo(app->fb, 3, 4, th, NULL);
    render_blit(c, app->fb);
    draw_title_texts(app, c, 51, 1.0f);
}

static void start_game(App* app) {
    game_reset(&app->game, &CAM_GAME);
    app->paused = false;
    app->new_best = false;
    app->over_fx_done = false;
}

static void help_reset(App* app) {
    static const DemoMode modes[] = {DemoHelpDrop, DemoHelpSlice, DemoHelpPerfect, DemoNone};
    app->help.demo = modes[app->help_page % 4];
    game_reset(&app->help, &CAM_HELP);
    if(app->help.demo == DemoNone) app->help.moving = false;
}

static void title_input(App* app, const InputEvent* ev) {
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    switch(ev->key) {
    case InputKeyLeft:
        app->title_sel = (app->title_sel + TitleCount - 1) % TitleCount;
        fx_nav(app);
        break;
    case InputKeyRight:
        app->title_sel = (app->title_sel + 1) % TitleCount;
        fx_nav(app);
        break;
    case InputKeyOk:
        if(ev->type != InputTypeShort) break;
        fx_select(app);
        switch(app->title_sel) {
        case TitlePlay:
            start_game(app);
            screens_enter(app, ScrGame, true);
            break;
        case TitleSettings:
            app->set_sel = 0;
            app->set_top = 0;
            app->confirm = false;
            screens_enter(app, ScrSettings, true);
            break;
        case TitleStats:
            app->stat_top = 0;
            screens_enter(app, ScrStats, true);
            break;
        case TitleHelp:
            app->help_page = 0;
            help_reset(app);
            screens_enter(app, ScrHelp, true);
            break;
        }
        break;
    case InputKeyBack:
        if(ev->type == InputTypeShort) app->running = false;
        break;
    default:
        break;
    }
}

/* ---------- Game ---------- */

static void game_events(App* app) {
    Game* g = &app->game;
    uint8_t ev = g->events;
    g->events = 0;
    if(!ev) return;
    if(ev & EV_MISS) {
        SaveData* s = &app->save;
        s->games++;
        s->blocks += g->score;
        s->perfects += g->perfects;
        if(g->max_combo > s->best_streak) s->best_streak = g->max_combo;
        app->new_best = g->score > s->best;
        if(app->new_best) s->best = g->score;
        store_save(app);
        fx_miss(app);
    } else if(ev & EV_GROW) {
        fx_grow(app, g->combo);
    } else if(ev & EV_PERFECT) {
        fx_perfect(app, g->combo);
    } else if(ev & EV_PLACE) {
        fx_place(app);
    }
}

static void game_screen_update(App* app, uint32_t dt) {
    Game* g = &app->game;
    if(app->paused) return;
    game_update(g, dt);
    game_events(app);
    if(g->phase == GsOver && !app->over_fx_done && g->phase_t >= PANEL_MS) {
        app->over_fx_done = true;
        fx_result(app, app->new_best);
    }
}

static void draw_score(App* app, Canvas* c, const Game* g) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%u", g->score);
    int dy = 0;
    if(g->pop_t < POP_MS) dy = -(int)(3.0f * (1.0f - (float)g->pop_t / POP_MS) + 0.5f);
    canvas_set_font(c, FontBigNumbers);
    halo_text(c, 64, 17 + dy, AlignCenter, AlignBottom, buf, fg(app), bg(app));
}

static void draw_pause(App* app, Canvas* c) {
    Color f = fg(app), b = bg(app);
    render_fade(c, 0, 0, 128, 64, 8, b == ColorBlack);
    panel(c, 26, 5, 76, 54, f, b);
    canvas_set_color(c, f);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 15, AlignCenter, AlignBottom, "PAUSED");
    canvas_draw_line(c, 34, 18, 94, 18);
    static const char* items[] = {"Resume", "Restart", "Menu"};
    canvas_set_font(c, FontSecondary);
    for(int i = 0; i < 3; i++) {
        int y = 21 + i * 12;
        if(app->pause_sel == i) {
            canvas_set_color(c, f);
            canvas_draw_rbox(c, 31, y, 66, 11, 2);
            canvas_set_color(c, b);
        } else {
            canvas_set_color(c, f);
        }
        canvas_draw_str_aligned(c, 64, y + 2, AlignCenter, AlignTop, items[i]);
    }
}

static void draw_game_over(App* app, Canvas* c, const Game* g) {
    Color f = fg(app), b = bg(app);
    char buf[24];
    float k = ease_out_cubic((float)g->phase_t / PANEL_MS);
    int x = 75 + (int)((1.0f - k) * 56.0f);
    panel(c, x, 1, 52, 62, f, b);
    int cx = x + 26;

    canvas_set_color(c, f);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, cx, 3, AlignCenter, AlignTop, "SCORE");
    snprintf(buf, sizeof(buf), "%u", g->score);
    canvas_set_font(c, FontBigNumbers);
    canvas_draw_str_aligned(c, cx, 27, AlignCenter, AlignBottom, buf);

    canvas_set_font(c, FontSecondary);
    if(app->new_best) {
        bool on = ((g->phase_t / 350) % 4) != 3;
        if(on) {
            canvas_draw_rbox(c, x + 2, 30, 48, 11, 2);
            canvas_set_color(c, b);
        }
        canvas_draw_str_aligned(c, cx + 1, 32, AlignCenter, AlignTop, "NEW BEST");
        canvas_set_color(c, f);
    } else {
        snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)app->save.best);
        canvas_draw_str_aligned(c, cx, 32, AlignCenter, AlignTop, buf);
    }
    canvas_draw_line(c, x + 5, 43, x + 46, 43);
    key_hint(c, x + 5, 45, "OK", "Retry", f, b);
    back_hint(c, x + 5, 53, "Menu", f);
}

static void game_draw(App* app, Canvas* c) {
    Game* g = &app->game;
    Theme th = theme_of(app);
    render_background(app->fb, th, g->cam.camy, app->now);
    render_scene(app->fb, g, th, true, -1, NULL, 0.0f);
    render_blit(c, app->fb);

    if(g->phase == GsPlaying || g->phase == GsFalling) {
        draw_score(app, c, g);
    }
    if(g->phase == GsPlaying && g->score == 0 && app->save.games < 3 && !app->paused) {
        if((app->screen_t / 500) % 3 != 2) {
            canvas_set_font(c, FontSecondary);
            int w = canvas_string_width(c, "OK") + 5 + 3 + canvas_string_width(c, "to drop");
            panel(c, 64 - w / 2 - 4, 51, w + 8, 12, fg(app), bg(app));
            key_hint(c, 64 - w / 2, 52, "OK", "to drop", fg(app), bg(app));
        }
    }
    if(g->phase == GsOver) draw_game_over(app, c, g);
    if(app->paused) draw_pause(app, c);
}

static void game_input(App* app, const InputEvent* ev) {
    Game* g = &app->game;

    if(app->paused) {
        if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
        if(ev->key == InputKeyUp) {
            app->pause_sel = (app->pause_sel + 2) % 3;
            fx_nav(app);
        } else if(ev->key == InputKeyDown) {
            app->pause_sel = (app->pause_sel + 1) % 3;
            fx_nav(app);
        } else if(ev->key == InputKeyBack && ev->type == InputTypeShort) {
            app->paused = false;
            fx_back(app);
        } else if(ev->key == InputKeyOk && ev->type == InputTypeShort) {
            fx_select(app);
            if(app->pause_sel == 0) {
                app->paused = false;
            } else if(app->pause_sel == 1) {
                start_game(app);
                screens_enter(app, ScrGame, true);
            } else {
                app->paused = false;
                screens_enter(app, ScrTitle, true);
            }
        }
        return;
    }

    switch(g->phase) {
    case GsPlaying:
        if(ev->key == InputKeyBack) {
            if(ev->type == InputTypeShort) {
                app->paused = true;
                app->pause_sel = 0;
                fx_pause(app);
            }
        } else if(ev->type == InputTypePress) {
            game_drop(g);
            game_events(app);
        }
        break;
    case GsFalling:
    case GsZoom:
        if(ev->type == InputTypeShort && ev->key == InputKeyOk && g->phase_t > 250) {
            game_skip_zoom(g);
        }
        break;
    case GsOver:
        if(ev->type != InputTypeShort || g->phase_t < PANEL_MS) break;
        if(ev->key == InputKeyOk) {
            fx_select(app);
            start_game(app);
            screens_enter(app, ScrGame, true);
        } else if(ev->key == InputKeyBack) {
            fx_back(app);
            screens_enter(app, ScrTitle, true);
        }
        break;
    default:
        break;
    }
}

/* ---------- Settings ---------- */

typedef enum {
    SetSound,
    SetVolume,
    SetVibro,
    SetLed,
    SetTheme,
    SetIntro,
    SetReset,
    SetCount,
} SetItem;

#define LIST_Y 14
#define LIST_ROW 12
#define LIST_VISIBLE 4

static const char* on_off(uint8_t v) {
    return v ? "On" : "Off";
}

static void setting_value(App* app, int i, char* buf, size_t n) {
    static const char* vols[] = {"Low", "Medium", "High"};
    static const char* themes[] = {"Day", "Dots", "Night"};
    SaveData* s = &app->save;
    switch(i) {
    case SetSound:
        snprintf(buf, n, "%s", on_off(s->sound));
        break;
    case SetVolume:
        snprintf(buf, n, "%s", vols[s->volume % VolCount]);
        break;
    case SetVibro:
        snprintf(buf, n, "%s", on_off(s->vibro));
        break;
    case SetLed:
        snprintf(buf, n, "%s", on_off(s->led));
        break;
    case SetTheme:
        snprintf(buf, n, "%s", themes[s->theme % ThemeCount]);
        break;
    case SetIntro:
        snprintf(buf, n, "%s", on_off(s->intro));
        break;
    default:
        buf[0] = 0;
        break;
    }
}

static void draw_header(App* app, Canvas* c, const uint16_t* icon, const char* title) {
    Color f = fg(app);
    canvas_set_color(c, f);
    draw_icon(c, 3, 1, icon, 9, 9);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 16, 10, title);
    canvas_draw_line(c, 0, 12, 127, 12);
}

static void draw_list_row(App* app, Canvas* c, int row, bool sel, const char* label, const char* value, bool arrows) {
    Color f = fg(app), b = bg(app);
    int y = LIST_Y + row * LIST_ROW;
    canvas_set_font(c, FontSecondary);
    if(sel) {
        canvas_set_color(c, f);
        canvas_draw_rbox(c, 1, y, 121, LIST_ROW - 1, 2);
        canvas_set_color(c, b);
    } else {
        canvas_set_color(c, f);
    }
    canvas_draw_str_aligned(c, 5, y + 2, AlignLeft, AlignTop, label);
    if(value && value[0]) {
        if(arrows) {
            int vw = canvas_string_width(c, value);
            canvas_draw_str_aligned(c, 117, y + 2, AlignRight, AlignTop, ">");
            canvas_draw_str_aligned(c, 111, y + 2, AlignRight, AlignTop, value);
            canvas_draw_str_aligned(c, 111 - vw - 3, y + 2, AlignRight, AlignTop, "<");
        } else {
            canvas_draw_str_aligned(c, 117, y + 2, AlignRight, AlignTop, value);
        }
    }
}

static void settings_draw(App* app, Canvas* c) {
    static const char* labels[] = {
        "Sound", "Volume", "Vibration", "LED", "Background", "Intro animation", "Reset stats"};
    Color f = fg(app), b = bg(app);
    char val[16];
    if(b == ColorBlack) {
        canvas_set_color(c, ColorBlack);
        canvas_draw_box(c, 0, 0, 128, 64);
    }
    draw_header(app, c, ICON_GEAR, "SETTINGS");
    for(int r = 0; r < LIST_VISIBLE; r++) {
        int i = app->set_top + r;
        if(i >= SetCount) break;
        bool sel = app->set_sel == i;
        if(i == SetReset) {
            draw_list_row(app, c, r, sel, labels[i], sel ? "OK" : "...", false);
        } else {
            setting_value(app, i, val, sizeof(val));
            draw_list_row(app, c, r, sel, labels[i], val, sel);
        }
    }
    scrollbar(c, 124, LIST_Y, LIST_VISIBLE * LIST_ROW - 1, app->set_top, SetCount, LIST_VISIBLE, f);

    if(app->confirm) {
        render_fade(c, 0, 0, 128, 64, 8, b == ColorBlack);
        panel(c, 12, 9, 104, 46, f, b);
        canvas_set_color(c, f);
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, 64, 19, AlignCenter, AlignBottom, "Reset stats?");
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 22, AlignCenter, AlignTop, "Best score and totals");
        static const char* opts[] = {"No", "Yes"};
        for(int i = 0; i < 2; i++) {
            int x = 22 + i * 44;
            bool sel = app->confirm_sel == i;
            canvas_set_color(c, f);
            if(sel)
                canvas_draw_rbox(c, x, 37, 40, 13, 3);
            else
                canvas_draw_rframe(c, x, 37, 40, 13, 3);
            canvas_set_color(c, sel ? b : f);
            canvas_set_font(c, FontPrimary);
            canvas_draw_str_aligned(c, x + 20, 47, AlignCenter, AlignBottom, opts[i]);
        }
    }
}

static void settings_change(App* app, int dir) {
    SaveData* s = &app->save;
    switch(app->set_sel) {
    case SetSound:
        s->sound = !s->sound;
        fx_toggle(app, s->sound);
        break;
    case SetVolume:
        s->volume = (uint8_t)((s->volume + VolCount + dir) % VolCount);
        fx_test_volume(app);
        break;
    case SetVibro:
        s->vibro = !s->vibro;
        fx_toggle(app, s->vibro);
        fx_test_vibro(app);
        break;
    case SetLed:
        s->led = !s->led;
        fx_toggle(app, s->led);
        fx_test_led(app);
        break;
    case SetTheme:
        s->theme = (uint8_t)((s->theme + ThemeCount + dir) % ThemeCount);
        fx_nav(app);
        break;
    case SetIntro:
        s->intro = !s->intro;
        fx_toggle(app, s->intro);
        break;
    default:
        break;
    }
}

static void settings_input(App* app, const InputEvent* ev) {
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;

    if(app->confirm) {
        if(ev->type != InputTypeShort) return;
        if(ev->key == InputKeyLeft || ev->key == InputKeyRight) {
            app->confirm_sel ^= 1;
            fx_nav(app);
        } else if(ev->key == InputKeyOk) {
            if(app->confirm_sel == 1) {
                store_reset_stats(app);
                fx_select(app);
            } else {
                fx_back(app);
            }
            app->confirm = false;
        } else if(ev->key == InputKeyBack) {
            app->confirm = false;
            fx_back(app);
        }
        return;
    }

    switch(ev->key) {
    case InputKeyUp:
        app->set_sel = (app->set_sel + SetCount - 1) % SetCount;
        fx_nav(app);
        break;
    case InputKeyDown:
        app->set_sel = (app->set_sel + 1) % SetCount;
        fx_nav(app);
        break;
    case InputKeyLeft:
    case InputKeyRight:
        if(ev->type == InputTypeShort && app->set_sel != SetReset) {
            settings_change(app, ev->key == InputKeyRight ? 1 : -1);
        }
        break;
    case InputKeyOk:
        if(ev->type != InputTypeShort) break;
        if(app->set_sel == SetReset) {
            app->confirm = true;
            app->confirm_sel = 0;
            fx_select(app);
        } else {
            settings_change(app, 1);
        }
        break;
    case InputKeyBack:
        if(ev->type == InputTypeShort) {
            store_save(app);
            fx_back(app);
            screens_enter(app, ScrTitle, true);
        }
        break;
    default:
        break;
    }
    if(app->set_sel < app->set_top) app->set_top = app->set_sel;
    if(app->set_sel >= app->set_top + LIST_VISIBLE) app->set_top = app->set_sel - LIST_VISIBLE + 1;
}

/* ---------- Statistics ---------- */

#define STAT_COUNT 7

static void stats_draw(App* app, Canvas* c) {
    static const char* labels[STAT_COUNT] = {
        "Best score",
        "Games played",
        "Slabs stacked",
        "Perfect drops",
        "Best perfect streak",
        "Average score",
        "Perfect rate"};
    SaveData* s = &app->save;
    Color f = fg(app), b = bg(app);
    char val[16];
    if(b == ColorBlack) {
        canvas_set_color(c, ColorBlack);
        canvas_draw_box(c, 0, 0, 128, 64);
    }
    draw_header(app, c, ICON_TROPHY, "STATS");
    for(int r = 0; r < LIST_VISIBLE; r++) {
        int i = app->stat_top + r;
        if(i >= STAT_COUNT) break;
        switch(i) {
        case 0:
            snprintf(val, sizeof(val), "%lu", (unsigned long)s->best);
            break;
        case 1:
            snprintf(val, sizeof(val), "%lu", (unsigned long)s->games);
            break;
        case 2:
            snprintf(val, sizeof(val), "%lu", (unsigned long)s->blocks);
            break;
        case 3:
            snprintf(val, sizeof(val), "%lu", (unsigned long)s->perfects);
            break;
        case 4:
            snprintf(val, sizeof(val), "%lu", (unsigned long)s->best_streak);
            break;
        case 5: {
            unsigned long avg10 = s->games ? (unsigned long)(s->blocks * 10u / s->games) : 0;
            snprintf(val, sizeof(val), "%lu.%lu", avg10 / 10, avg10 % 10);
            break;
        }
        default: {
            unsigned long pct = s->blocks ? (unsigned long)(s->perfects * 100u / s->blocks) : 0;
            snprintf(val, sizeof(val), "%lu%%", pct);
            break;
        }
        }
        draw_list_row(app, c, r, false, labels[i], val, false);
        canvas_set_color(c, f);
        if(r < LIST_VISIBLE - 1 && i < STAT_COUNT - 1) {
            int y = LIST_Y + r * LIST_ROW + LIST_ROW - 1;
            for(int x = 5; x < 118; x += 2) canvas_draw_dot(c, x, y);
        }
    }
    scrollbar(c, 124, LIST_Y, LIST_VISIBLE * LIST_ROW - 1, app->stat_top, STAT_COUNT, LIST_VISIBLE, f);
}

static void stats_input(App* app, const InputEvent* ev) {
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return;
    if(ev->key == InputKeyUp && app->stat_top > 0) {
        app->stat_top--;
        fx_nav(app);
    } else if(ev->key == InputKeyDown && app->stat_top + LIST_VISIBLE < STAT_COUNT) {
        app->stat_top++;
        fx_nav(app);
    } else if((ev->key == InputKeyBack || ev->key == InputKeyOk) && ev->type == InputTypeShort) {
        fx_back(app);
        screens_enter(app, ScrTitle, true);
    }
}

/* ---------- How to play ---------- */

#define HELP_PAGES 4

static void help_draw(App* app, Canvas* c) {
    static const char* titles[HELP_PAGES] = {"DROP", "SLICE", "PERFECT", "ABOUT"};
    static const char* lines[HELP_PAGES][4] = {
        {"Press OK to drop", "the sliding slab", "onto the tower.", "Stack it high!"},
        {"Whatever hangs", "over the edge is", "sliced off. Miss", "and it's over."},
        {"Land it exactly:", "no loss at all.", "8 perfects in a", "row: it grows!"},
        {NULL, NULL, NULL, NULL},
    };
    Theme th = theme_of(app);
    Color f = fg(app), b = bg(app);
    uint8_t p = app->help_page;

    render_background(app->fb, th, app->help.cam.camy, app->now);
    if(p < 3) {
        render_scene(app->fb, &app->help, th, true, -1, NULL, 0.0f);
    } else {
        render_logo(app->fb, 35, 5, th, NULL);
    }
    render_blit(c, app->fb);

    canvas_set_color(c, f);
    if(p < 3) {
        /* text column on a clean background */
        canvas_set_color(c, b);
        canvas_draw_box(c, 0, 0, 66, 53);
        canvas_set_color(c, f);
        canvas_set_font(c, FontPrimary);
        canvas_draw_str(c, 3, 10, titles[p]);
        canvas_draw_line(c, 3, 12, 3 + canvas_string_width(c, titles[p]), 12);
        canvas_set_font(c, FontSecondary);
        for(int i = 0; i < 4; i++) {
            canvas_draw_str(c, 3, 22 + i * 9, lines[p][i]);
        }
    } else {
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 26, AlignCenter, AlignTop, "TOWER  v" APP_VERSION);
        canvas_draw_str_aligned(c, 64, 35, AlignCenter, AlignTop, "made by Toni");
        canvas_draw_str_aligned(c, 64, 44, AlignCenter, AlignTop, "inspired by Stack");
    }

    /* page indicator */
    canvas_set_color(c, b);
    canvas_draw_box(c, 0, 54, 128, 10);
    canvas_set_color(c, f);
    int x0 = 64 - (HELP_PAGES * 7 - 3) / 2;
    for(int i = 0; i < HELP_PAGES; i++) {
        if(i == p)
            canvas_draw_rbox(c, x0 + i * 7, 57, 4, 4, 1);
        else
            canvas_draw_rframe(c, x0 + i * 7, 57, 4, 4, 1);
    }
    canvas_set_font(c, FontSecondary);
    if(p > 0) canvas_draw_str(c, 2, 62, "<");
    if(p < HELP_PAGES - 1) canvas_draw_str_aligned(c, 125, 62, AlignRight, AlignBottom, ">");
}

static void help_input(App* app, const InputEvent* ev) {
    if(ev->type != InputTypeShort) return;
    if(ev->key == InputKeyRight || ev->key == InputKeyOk) {
        if(app->help_page + 1 < HELP_PAGES) {
            app->help_page++;
            help_reset(app);
            fx_nav(app);
        } else if(ev->key == InputKeyOk) {
            fx_back(app);
            screens_enter(app, ScrTitle, true);
        }
    } else if(ev->key == InputKeyLeft) {
        if(app->help_page > 0) {
            app->help_page--;
            help_reset(app);
            fx_nav(app);
        }
    } else if(ev->key == InputKeyBack) {
        fx_back(app);
        screens_enter(app, ScrTitle, true);
    }
}

/* ---------- Dispatch ---------- */

void screens_start(App* app) {
    if(app->save.intro) {
        screens_enter(app, ScrIntro, false);
    } else {
        game_reset(&app->demo, &CAM_TITLE);
        screens_enter(app, ScrTitle, true);
    }
}

void screens_enter(App* app, ScreenId s, bool fade) {
    if(s == ScrIntro) intro_prepare(app);
    if(s == ScrTitle && app->demo.phase == GsPlaying) game_start_moving(&app->demo);
    app->screen = s;
    app->screen_t = 0;
    app->fading = fade;
    app->fade_t = 0;
}

void screens_update(App* app, uint32_t dt) {
    app->screen_t += dt;
    if(app->fading) {
        app->fade_t += dt;
        if(app->fade_t >= FADE_MS) app->fading = false;
    }
    switch(app->screen) {
    case ScrIntro:
        intro_update(app);
        break;
    case ScrTitle:
        game_update(&app->demo, dt);
        app->demo.events = 0;
        break;
    case ScrGame:
        game_screen_update(app, dt);
        break;
    case ScrHelp:
        game_update(&app->help, dt);
        app->help.events = 0;
        break;
    default:
        break;
    }
}

void screens_draw(App* app, Canvas* c) {
    canvas_set_bitmap_mode(c, false);
    switch(app->screen) {
    case ScrIntro:
        intro_draw(app, c);
        break;
    case ScrTitle:
        title_draw(app, c);
        break;
    case ScrGame:
        game_draw(app, c);
        break;
    case ScrSettings:
        settings_draw(app, c);
        break;
    case ScrStats:
        stats_draw(app, c);
        break;
    case ScrHelp:
        help_draw(app, c);
        break;
    }
    if(app->fading) {
        float k = 1.0f - (float)app->fade_t / FADE_MS;
        render_fade(c, 0, 0, 128, 64, (uint8_t)(k * 16.0f + 0.5f), bg(app) == ColorBlack);
    }
}

void screens_input(App* app, const InputEvent* ev) {
    switch(app->screen) {
    case ScrIntro:
        if(ev->type == InputTypeShort) {
            if(ev->key == InputKeyBack) {
                app->running = false;
            } else {
                game_start_moving(&app->demo);
                screens_enter(app, ScrTitle, true);
            }
        }
        break;
    case ScrTitle:
        title_input(app, ev);
        break;
    case ScrGame:
        game_input(app, ev);
        break;
    case ScrSettings:
        settings_input(app, ev);
        break;
    case ScrStats:
        stats_input(app, ev);
        break;
    case ScrHelp:
        help_input(app, ev);
        break;
    }
}
