/*
 * 1-bit framebuffer + isometric 3D renderer.
 *
 * Everything 3D is drawn into our own 128x64 framebuffer (1 bit per pixel,
 * XBM layout, bit set = black pixel) and then blitted onto the canvas in a
 * single canvas_draw_xbm() call. Text and UI are drawn on top with the
 * normal canvas API afterwards.
 *
 * Slabs are drawn with the painter's algorithm, bottom to top: each slab
 * shows its top face (white), left face (white, black edge lines) and
 * right face (black with a white highlight line between layers).
 */
#include "stack.h"

static const uint8_t BAYER[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

typedef struct {
    int x, y;
} IPt;

typedef struct {
    uint8_t top, left, right; /* Bayer levels 0 (white) .. 16 (black) */
    bool ink; /* line colour: true = black */
    bool rsep; /* white separator line on the dark right face */
} Style;

static Style style_for(Theme theme) {
    if(theme == ThemeNight) return (Style){0, 0, 9, true, false};
    return (Style){0, 0, 16, true, true};
}

/* ---------- Framebuffer primitives ---------- */

static inline void px(uint8_t* fb, int x, int y, bool black) {
    if((unsigned)x >= 128 || (unsigned)y >= 64) return;
    uint8_t* b = &fb[y * 16 + (x >> 3)];
    uint8_t m = (uint8_t)(1 << (x & 7));
    if(black)
        *b |= m;
    else
        *b &= (uint8_t)~m;
}

void fb_clear(uint8_t* fb, bool black) {
    memset(fb, black ? 0xFF : 0x00, 128 * 64 / 8);
}

void fb_span(uint8_t* fb, int x0, int x1, int y, uint8_t lvl) {
    if(y < 0 || y > 63) return;
    if(x0 < 0) x0 = 0;
    if(x1 > 127) x1 = 127;
    const uint8_t* row = BAYER[y & 3];
    for(int x = x0; x <= x1; x++) {
        px(fb, x, y, row[x & 3] < lvl);
    }
}

void fb_rect_pattern(uint8_t* fb, int x, int y, int w, int h, uint8_t lvl) {
    for(int j = 0; j < h; j++) {
        fb_span(fb, x, x + w - 1, y + j, lvl);
    }
}

void fb_line(uint8_t* fb, int x0, int y0, int x1, int y1, bool black, uint8_t dash) {
    if((x0 < 0 && x1 < 0) || (x0 > 127 && x1 > 127) || (y0 < 0 && y1 < 0) ||
       (y0 > 63 && y1 > 63))
        return;
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int n = 0;
    for(;;) {
        if(!dash || (n % dash) == 0) px(fb, x0, y0, black);
        n++;
        if(x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if(e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if(e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* Convex polygon, integer vertices, pixel centres on the edge are inside. */
static void fb_poly(uint8_t* fb, const IPt* p, int n, uint8_t lvl) {
    int ymin = p[0].y, ymax = p[0].y;
    for(int i = 1; i < n; i++) {
        if(p[i].y < ymin) ymin = p[i].y;
        if(p[i].y > ymax) ymax = p[i].y;
    }
    if(ymin < 0) ymin = 0;
    if(ymax > 63) ymax = 63;
    for(int y = ymin; y <= ymax; y++) {
        float xl = 1e9f, xr = -1e9f;
        for(int i = 0; i < n; i++) {
            IPt a = p[i];
            IPt b = p[(i + 1) % n];
            if((a.y <= y && y <= b.y) || (b.y <= y && y <= a.y)) {
                if(a.y == b.y) {
                    xl = fminf(xl, (float)(a.x < b.x ? a.x : b.x));
                    xr = fmaxf(xr, (float)(a.x > b.x ? a.x : b.x));
                } else {
                    float x = (float)a.x +
                              (float)(y - a.y) * (float)(b.x - a.x) / (float)(b.y - a.y);
                    xl = fminf(xl, x);
                    xr = fmaxf(xr, x);
                }
            }
        }
        if(xl <= xr) {
            fb_span(fb, (int)ceilf(xl - 1e-4f), (int)floorf(xr + 1e-4f), y, lvl);
        }
    }
}

/* ---------- Projection ---------- */

static IPt proj(const Cam* c, float x, float z, float y) {
    float sx = c->ox + (x - z) * 2.0f * c->zoom;
    float sy = c->oy + ((x + z) - (y - c->camy)) * c->zoom;
    if(sx < -20000.0f) sx = -20000.0f;
    if(sx > 20000.0f) sx = 20000.0f;
    if(sy < -20000.0f) sy = -20000.0f;
    if(sy > 20000.0f) sy = 20000.0f;
    IPt p = {(int)floorf(sx + 0.5f), (int)floorf(sy + 0.5f)};
    return p;
}

static void draw_slab(
    uint8_t* fb,
    const Cam* c,
    const Slab* s,
    float ybot,
    float ytop,
    const Style* st) {
    IPt t00 = proj(c, s->x0, s->z0, ytop);
    IPt t10 = proj(c, s->x1, s->z0, ytop);
    IPt t11 = proj(c, s->x1, s->z1, ytop);
    IPt t01 = proj(c, s->x0, s->z1, ytop);
    IPt b10 = proj(c, s->x1, s->z0, ybot);
    IPt b11 = proj(c, s->x1, s->z1, ybot);
    IPt b01 = proj(c, s->x0, s->z1, ybot);

    IPt right[4] = {t10, t11, b11, b10};
    IPt left[4] = {t01, t11, b11, b01};
    IPt top[4] = {t00, t10, t11, t01};
    fb_poly(fb, right, 4, st->right);
    fb_poly(fb, left, 4, st->left);
    fb_poly(fb, top, 4, st->top);

    const bool k = st->ink;
    fb_line(fb, t00.x, t00.y, t10.x, t10.y, k, 0);
    fb_line(fb, t10.x, t10.y, t11.x, t11.y, k, 0);
    fb_line(fb, t11.x, t11.y, t01.x, t01.y, k, 0);
    fb_line(fb, t01.x, t01.y, t00.x, t00.y, k, 0);
    fb_line(fb, t01.x, t01.y, b01.x, b01.y, k, 0);
    fb_line(fb, t10.x, t10.y, b10.x, b10.y, k, 0);

    /* Zoomed far out the slabs are only 1-2 px thick: skip the inner
     * edges so the tower keeps its light and dark sides. */
    bool thin = (b11.y - t11.y) < 3;
    if(!thin) {
        fb_line(fb, t11.x, t11.y, b11.x, b11.y, k, 0);
        fb_line(fb, b01.x, b01.y, b11.x, b11.y, k, 0);
        fb_line(fb, b11.x, b11.y, b10.x, b10.y, k, 0);
        if(st->rsep && b10.x - b11.x >= 2) {
            fb_line(fb, b11.x + 1, b11.y - 1, b10.x, b10.y - 1, false, 0);
        }
    }
}

static void draw_ring(uint8_t* fb, const Cam* c, const Ring* r, bool ink) {
    float t = (float)r->age / (float)RING_MS;
    float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    float m = 0.4f + 3.0f * e;
    IPt a = proj(c, r->s.x0 - m, r->s.z0 - m, r->y);
    IPt b = proj(c, r->s.x1 + m, r->s.z0 - m, r->y);
    IPt d = proj(c, r->s.x1 + m, r->s.z1 + m, r->y);
    IPt f = proj(c, r->s.x0 - m, r->s.z1 + m, r->y);
    uint8_t dash = t < 0.45f ? 0 : (t < 0.75f ? 2 : 3);
    fb_line(fb, a.x, a.y, b.x, b.y, ink, dash);
    fb_line(fb, b.x, b.y, d.x, d.y, ink, dash);
    fb_line(fb, d.x, d.y, f.x, f.y, ink, dash);
    fb_line(fb, f.x, f.y, a.x, a.y, ink, dash);
}

/* ---------- Backgrounds ---------- */

static uint32_t hash32(uint32_t v) {
    v = (v + 1u) * 2654435761u;
    v ^= v >> 13;
    v *= 0x5bd1e995u;
    v ^= v >> 15;
    return v;
}

static int pmod(int a, int m) {
    int r = a % m;
    return r < 0 ? r + m : r;
}

void render_background(uint8_t* fb, Theme theme, float camy, uint32_t t) {
    if(theme == ThemeNight) {
        fb_clear(fb, true);
        int shift = (int)(camy * 0.3f);
        for(uint32_t i = 0; i < 42; i++) {
            uint32_t h = hash32(i);
            int x = (int)(h % 128u);
            int y = pmod((int)((h >> 8) % 96u) + shift, 96);
            if(y >= 64) continue;
            if(((t / 170u) + (h >> 16)) % 13u == 0) continue; /* twinkle */
            px(fb, x, y, false);
            if(((h >> 20) & 7u) == 0) {
                px(fb, x - 1, y, false);
                px(fb, x + 1, y, false);
                px(fb, x, y - 1, false);
                px(fb, x, y + 1, false);
            }
        }
        return;
    }
    fb_clear(fb, false);
    if(theme == ThemeDots) {
        int shift = (int)(camy * 0.5f);
        for(int y = 0; y < 64; y++) {
            int ry = y - shift;
            if(pmod(ry, 8) != 0) continue;
            int row = (ry - pmod(ry, 8)) / 8;
            int ox = (row & 1) ? 8 : 0;
            for(int x = ox; x < 128; x += 16) {
                px(fb, x, y, true);
            }
        }
    }
}

/* ---------- Scene ---------- */

static void lerp_slab(Slab* out, const Slab* a, const Slab* b, float t) {
    out->x0 = a->x0 + (b->x0 - a->x0) * t;
    out->x1 = a->x1 + (b->x1 - a->x1) * t;
    out->z0 = a->z0 + (b->z0 - a->z0) * t;
    out->z1 = a->z1 + (b->z1 - a->z1) * t;
}

static void draw_pieces(uint8_t* fb, const Game* g, bool front, const Style* st) {
    for(int i = 0; i < MAX_PIECES; i++) {
        const Piece* p = &g->pieces[i];
        if(!p->active || p->front != front) continue;
        draw_slab(fb, &g->cam, &p->s, p->y, p->y + LAYER_TH, st);
    }
}

void render_scene(
    uint8_t* fb,
    const Game* g,
    Theme theme,
    bool show_cur,
    int draw_layers,
    const float* yoff,
    float pillar_off) {
    const Cam* c = &g->cam;
    const Style st = style_for(theme);
    const float base = (float)g->first_index * LAYER_TH;

    draw_pieces(fb, g, false, &st);

    /* Base pillar: reaches down past the bottom edge of the screen. */
    float ptop = base + pillar_off;
    float pbot = c->camy + BLOCK_S - (72.0f - c->oy) / c->zoom;
    if(pbot < ptop - 1.0f) {
        draw_slab(fb, c, &g->layers[0], pbot, ptop, &st);
    }

    int n = g->count;
    if(draw_layers >= 0 && draw_layers < n) n = draw_layers;
    for(int i = 1; i < n; i++) {
        float yb = base + (float)(i - 1) * LAYER_TH;
        if(yoff) yb += yoff[i];
        float yt = yb + LAYER_TH;
        Slab s = g->layers[i];
        if(i == g->count - 1 && g->growing) {
            float t = (float)g->grow_t / (float)GROW_MS;
            float e = 1.0f - (1.0f - t) * (1.0f - t);
            lerp_slab(&s, &g->grow_from, &g->layers[i], e);
        }
        float top_vy = c->oy + ((s.x0 + s.z0) - (yt - c->camy)) * c->zoom;
        float bot_vy = c->oy + ((s.x1 + s.z1) - (yb - c->camy)) * c->zoom;
        if(top_vy > 66.0f || bot_vy < -2.0f) continue;
        draw_slab(fb, c, &s, yb, yt, &st);
    }

    for(int i = 0; i < MAX_RINGS; i++) {
        if(g->rings[i].active) draw_ring(fb, c, &g->rings[i], theme != ThemeNight);
    }

    if(show_cur && g->moving && g->phase == GsPlaying) {
        Slab s = g->cur;
        if(g->axis) {
            s.z0 += g->off;
            s.z1 += g->off;
        } else {
            s.x0 += g->off;
            s.x1 += g->off;
        }
        float y = game_top_y(g);
        draw_slab(fb, c, &s, y, y + LAYER_TH, &st);
    }

    draw_pieces(fb, g, true, &st);
}

/* ---------- Logo ---------- */

/* 5x7 block letters, bit 4 = leftmost column. */
static const uint8_t LOGO[5][7] = {
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F}, /* C */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
};

void render_logo(uint8_t* fb, int x, int y, Theme theme, const float* letter_dy) {
    const bool ink = theme != ThemeNight;
    for(int l = 0; l < 5; l++) {
        int lx = x + l * 12;
        int ly = y + (letter_dy ? (int)floorf(letter_dy[l] + 0.5f) : 0);
        /* 3D depth: checker pattern shifted one pixel down-right */
        for(int r = 0; r < 7; r++) {
            for(int col = 0; col < 5; col++) {
                if(!(LOGO[l][r] & (0x10 >> col))) continue;
                for(int py = 0; py < 2; py++) {
                    for(int pxx = 0; pxx < 2; pxx++) {
                        int X = lx + col * 2 + pxx + 1;
                        int Y = ly + r * 2 + py + 1;
                        if(((X + Y) & 1) == 0) px(fb, X, Y, ink);
                    }
                }
            }
        }
        for(int r = 0; r < 7; r++) {
            for(int col = 0; col < 5; col++) {
                if(!(LOGO[l][r] & (0x10 >> col))) continue;
                for(int py = 0; py < 2; py++) {
                    for(int pxx = 0; pxx < 2; pxx++) {
                        px(fb, lx + col * 2 + pxx, ly + r * 2 + py, ink);
                    }
                }
            }
        }
    }
}

/* ---------- Canvas output ---------- */

void render_blit(Canvas* canvas, const uint8_t* fb) {
    canvas_set_color(canvas, ColorBlack);
    canvas_set_bitmap_mode(canvas, true);
    canvas_draw_xbm(canvas, 0, 0, 128, 64, fb);
    canvas_set_bitmap_mode(canvas, false);
}

/* Paints Bayer-dithered dots of one colour over a rectangle (fades). */
void render_fade(Canvas* canvas, int x, int y, int w, int h, uint8_t lvl, bool black) {
    if(lvl == 0 || w <= 0 || h <= 0) return;
    if(w > 128) w = 128;
    uint8_t mask[16 * 4];
    int bpr = (w + 7) / 8;
    memset(mask, 0, sizeof(mask));
    for(int r = 0; r < 4; r++) {
        for(int i = 0; i < w; i++) {
            if(BAYER[(y + r) & 3][(x + i) & 3] < lvl) {
                mask[r * bpr + (i >> 3)] |= (uint8_t)(1 << (i & 7));
            }
        }
    }
    canvas_set_color(canvas, black ? ColorBlack : ColorWhite);
    canvas_set_bitmap_mode(canvas, true);
    for(int yy = 0; yy < h; yy += 4) {
        int hb = h - yy < 4 ? h - yy : 4;
        canvas_draw_xbm(canvas, x, y + yy, w, hb, mask);
    }
    canvas_set_bitmap_mode(canvas, false);
    canvas_set_color(canvas, ColorBlack);
}
