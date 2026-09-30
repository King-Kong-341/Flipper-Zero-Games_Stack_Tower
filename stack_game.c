/*
 * Game rules, physics and camera.
 *
 * Rules (same as the original Stack):
 *   - a slab slides back and forth over the tower, alternating between
 *     the two horizontal axes every layer
 *   - press to drop it: the overhanging part is sliced off and falls
 *   - land within PERFECT_TOL of the slab below = "perfect": no loss at all
 *   - GROW_COMBO perfects in a row and every perfect after that make the
 *     slab grow back a little (up to the original size)
 *   - miss the tower completely and the game is over
 *   - every slab placed = 1 point
 */
#include "stack.h"

static float frand(Game* g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return (float)(g->rng >> 8) / 16777216.0f;
}

static float ease_in_out(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

float game_top_y(const Game* g) {
    return (float)(g->first_index + g->count - 1) * LAYER_TH;
}

static float slab_lo(const Slab* s, uint8_t axis) {
    return axis ? s->z0 : s->x0;
}

static float slab_hi(const Slab* s, uint8_t axis) {
    return axis ? s->z1 : s->x1;
}

static void slab_set(Slab* s, uint8_t axis, float lo, float hi) {
    if(axis) {
        s->z0 = lo;
        s->z1 = hi;
    } else {
        s->x0 = lo;
        s->x1 = hi;
    }
}

static void add_piece(Game* g, const Slab* s, float y, uint8_t axis, bool front, float vd, float vy) {
    for(int i = 0; i < MAX_PIECES; i++) {
        Piece* p = &g->pieces[i];
        if(p->active) continue;
        p->s = *s;
        p->y = y;
        p->vy = vy;
        p->vd = vd;
        p->axis = axis;
        p->front = front;
        p->active = true;
        return;
    }
}

static void add_ring(Game* g, const Slab* s, float y) {
    int slot = 0;
    uint32_t oldest = 0;
    for(int i = 0; i < MAX_RINGS; i++) {
        if(!g->rings[i].active) {
            slot = i;
            break;
        }
        if(g->rings[i].age >= oldest) {
            oldest = g->rings[i].age;
            slot = i;
        }
    }
    g->rings[slot].s = *s;
    g->rings[slot].y = y;
    g->rings[slot].age = 0;
    g->rings[slot].active = true;
}

/* ---------- Autopilot (title screen + how-to-play) ---------- */

static void demo_pick(Game* g) {
    const Slab* t = &g->layers[g->count - 1];
    bool small = (t->x1 - t->x0 < 4.0f) || (t->z1 - t->z0 < 4.0f);
    bool full = g->count >= g->cap - 1;
    float sign = frand(g) < 0.5f ? -1.0f : 1.0f;
    float r = frand(g);

    if(full || (small && g->demo != DemoHelpPerfect)) {
        g->demo_err = sign * (AMP - 1.0f); /* miss on purpose -> collapse */
        return;
    }
    switch(g->demo) {
    case DemoTitle:
        g->demo_err = (r < 0.6f) ? 0.0f : sign * (0.9f + frand(g) * 1.3f);
        break;
    case DemoHelpDrop:
        g->demo_err = (r < 0.75f) ? 0.0f : sign * (0.9f + frand(g) * 0.6f);
        break;
    case DemoHelpSlice:
        g->demo_err = sign * (1.4f + frand(g) * 1.2f);
        break;
    case DemoHelpPerfect:
        /* two slices first so the grow-back is visible afterwards */
        g->demo_err = (g->score < 2) ? sign * 1.6f : 0.0f;
        break;
    default:
        g->demo_err = 0.0f;
        break;
    }
}

static void demo_collapse(Game* g) {
    /* The slabs tumble off towards the viewer, lowest first so the
     * painter's order of the front pieces stays correct. */
    float base = (float)g->first_index * LAYER_TH;
    for(uint16_t i = 1; i < g->count; i++) {
        uint8_t axis = frand(g) < 0.5f ? 0 : 1;
        float vd = 1.0f + frand(g) * 4.0f;
        float vy = frand(g) * 50.0f;
        add_piece(g, &g->layers[i], base + (float)(i - 1) * LAYER_TH, axis, true, vd, vy);
    }
    g->count = 1;
    g->phase = GsCollapse;
    g->phase_t = 0;
}

/* ---------- Setup ---------- */

void game_init(Game* g, uint16_t cap, DemoMode demo) {
    memset(g, 0, sizeof(Game));
    g->layers = malloc(sizeof(Slab) * cap);
    g->cap = cap;
    g->demo = demo;
    g->rng = furi_hal_random_get();
}

void game_free(Game* g) {
    free(g->layers);
    g->layers = NULL;
}

static void spawn(Game* g) {
    g->cur = g->layers[g->count - 1];
    g->axis = (uint8_t)((g->first_index + g->count - 1) & 1);
    g->off = -AMP;
    g->dir = 1.0f;
    g->speed = SPEED_START + SPEED_STEP * (float)g->score;
    if(g->speed > SPEED_MAX) g->speed = SPEED_MAX;
    g->moving = true;
    g->since_spawn = 0;
    if(g->demo) demo_pick(g);
}

void game_reset(Game* g, const Cam* layout) {
    const float h = BLOCK_S / 2.0f;
    g->layers[0] = (Slab){-h, h, -h, h};
    g->count = 1;
    g->first_index = 0;
    g->score = 0;
    g->combo = 0;
    g->max_combo = 0;
    g->perfects = 0;
    g->phase = GsPlaying;
    g->phase_t = 0;
    g->growing = false;
    g->pop_t = POP_MS;
    g->events = 0;
    memset(g->pieces, 0, sizeof(g->pieces));
    memset(g->rings, 0, sizeof(g->rings));
    g->base = *layout;
    g->cam = *layout;
    g->cam.camy = 0.0f;
    spawn(g);
}

/* Pre-stacked tower for the intro: `slabs` perfect layers, nothing moving. */
void game_build_intro(Game* g, const Cam* layout, uint8_t slabs) {
    game_reset(g, layout);
    g->moving = false;
    for(uint8_t i = 0; i < slabs && g->count < g->cap; i++) {
        g->layers[g->count] = g->layers[0];
        g->count++;
    }
    g->cam.camy = game_top_y(g);
}

void game_start_moving(Game* g) {
    if(!g->moving && g->phase == GsPlaying) spawn(g);
}

/* ---------- The drop ---------- */

void game_drop(Game* g) {
    if(!g->moving || g->phase != GsPlaying) return;

    const Slab top = g->layers[g->count - 1];
    const uint8_t a = g->axis;
    const float off = g->off;
    const float y = game_top_y(g);
    const float lp = slab_lo(&top, a);
    const float hp = slab_hi(&top, a);
    Slab placed = top;
    g->moving = false;

    if(fabsf(off) <= PERFECT_TOL) {
        /* Perfect: snaps onto the slab below, nothing is lost. */
        g->combo++;
        g->perfects++;
        if(g->combo > g->max_combo) g->max_combo = g->combo;
        g->events |= EV_PERFECT;
        if(g->combo >= GROW_COMBO) {
            float cx = (placed.x0 + placed.x1) / 2.0f;
            float cz = (placed.z0 + placed.z1) / 2.0f;
            float wx = fminf(BLOCK_S, placed.x1 - placed.x0 + GROW_AMOUNT);
            float wz = fminf(BLOCK_S, placed.z1 - placed.z0 + GROW_AMOUNT);
            if(wx > placed.x1 - placed.x0 + 0.01f || wz > placed.z1 - placed.z0 + 0.01f) {
                g->grow_from = placed;
                placed.x0 = cx - wx / 2.0f;
                placed.x1 = cx + wx / 2.0f;
                placed.z0 = cz - wz / 2.0f;
                placed.z1 = cz + wz / 2.0f;
                g->growing = true;
                g->grow_t = 0;
                g->events |= EV_GROW;
            }
        }
        add_ring(g, &placed, y + LAYER_TH);
    } else {
        const float lc = lp + off;
        const float hc = hp + off;
        const float lo = fmaxf(lc, lp);
        const float hi = fminf(hc, hp);
        g->combo = 0;

        if(hi - lo < MIN_SIZE) {
            /* Missed the tower: the whole slab falls, game over. */
            Slab miss = top;
            slab_set(&miss, a, lc, hc);
            add_piece(g, &miss, y, a, off > 0.0f, off > 0.0f ? 2.5f : -2.5f, 0.0f);
            g->events |= EV_MISS;
            g->phase_t = 0;
            if(g->demo) {
                demo_collapse(g);
            } else {
                g->phase = GsFalling;
            }
            return;
        }

        slab_set(&placed, a, lo, hi);
        Slab cut = top;
        if(off > 0.0f) {
            slab_set(&cut, a, hi, hc);
            add_piece(g, &cut, y, a, true, 2.0f, 0.0f);
        } else {
            slab_set(&cut, a, lc, lo);
            add_piece(g, &cut, y, a, false, -2.0f, 0.0f);
        }
        g->events |= EV_PLACE;
    }

    if(g->count >= g->cap) {
        /* Marathon game: forget the lower half of the tower. */
        uint16_t drop = g->cap / 2;
        memmove(g->layers, g->layers + drop, sizeof(Slab) * (g->count - drop));
        g->count -= drop;
        g->first_index += drop;
    }
    g->layers[g->count++] = placed;
    g->score++;
    g->pop_t = 0;
    spawn(g);
}

/* ---------- Camera ---------- */

static Cam zoom_target(const Game* g) {
    float top = game_top_y(g);
    float base = (float)g->first_index * LAYER_TH;
    float h_world = (top - base) + PILLAR_VIS + BLOCK_S * 2.0f;
    float z = 54.0f / h_world;
    if(z > 1.0f) z = 1.0f;
    float h = h_world * z;
    float margin = (64.0f - h) / 2.0f;
    Cam c = {.ox = 38.0f, .oy = margin + BLOCK_S * z, .zoom = z, .camy = top};
    return c;
}

void game_skip_zoom(Game* g) {
    if(g->phase == GsFalling) {
        g->cam1 = zoom_target(g);
    }
    if(g->phase == GsFalling || g->phase == GsZoom) {
        g->cam = g->cam1;
        g->phase = GsOver;
        g->phase_t = 0;
    }
}

/* ---------- Per-frame update ---------- */

void game_update(Game* g, uint32_t dt) {
    const float s = (float)dt / 1000.0f;
    g->phase_t += dt;
    g->since_spawn += dt;
    if(g->pop_t < POP_MS) g->pop_t += dt;

    if(g->moving && g->phase == GsPlaying) {
        float prev = g->off;
        g->off += g->dir * g->speed * s;
        if(g->off > AMP) {
            g->off = 2.0f * AMP - g->off;
            g->dir = -1.0f;
        } else if(g->off < -AMP) {
            g->off = -2.0f * AMP - g->off;
            g->dir = 1.0f;
        }
        if(g->demo && g->since_spawn >= DEMO_WAIT_MS) {
            float e = g->demo_err;
            if((prev - e) * (g->off - e) <= 0.0f && prev != g->off) {
                g->off = e;
                game_drop(g);
            }
        }
    }

    for(int i = 0; i < MAX_PIECES; i++) {
        Piece* p = &g->pieces[i];
        if(!p->active) continue;
        p->vy += GRAVITY * s;
        p->y -= p->vy * s;
        float d = p->vd * s;
        if(p->axis) {
            p->s.z0 += d;
            p->s.z1 += d;
        } else {
            p->s.x0 += d;
            p->s.x1 += d;
        }
        float sy =
            g->cam.oy + ((p->s.x0 + p->s.z0) - (p->y + LAYER_TH - g->cam.camy)) * g->cam.zoom;
        if(sy > 72.0f) p->active = false;
    }

    for(int i = 0; i < MAX_RINGS; i++) {
        Ring* r = &g->rings[i];
        if(!r->active) continue;
        r->age += dt;
        if(r->age >= RING_MS) r->active = false;
    }

    if(g->growing) {
        g->grow_t += dt;
        if(g->grow_t >= GROW_MS) g->growing = false;
    }

    switch(g->phase) {
    case GsPlaying:
    case GsFalling:
    case GsCollapse: {
        float target = game_top_y(g);
        float k = fminf(1.0f, s * 10.0f);
        g->cam.camy += (target - g->cam.camy) * k;
        if(g->phase == GsFalling && g->phase_t >= FALL_MS) {
            g->cam0 = g->cam;
            g->cam1 = zoom_target(g);
            g->phase = GsZoom;
            g->phase_t = 0;
        } else if(g->phase == GsCollapse && g->phase_t >= COLLAPSE_MS) {
            game_reset(g, &g->base);
        }
        break;
    }
    case GsZoom: {
        float t = (float)g->phase_t / (float)ZOOM_MS;
        if(t >= 1.0f) {
            g->cam = g->cam1;
            g->phase = GsOver;
            g->phase_t = 0;
        } else {
            float e = ease_in_out(t);
            g->cam.ox = g->cam0.ox + (g->cam1.ox - g->cam0.ox) * e;
            g->cam.oy = g->cam0.oy + (g->cam1.oy - g->cam0.oy) * e;
            g->cam.zoom = g->cam0.zoom + (g->cam1.zoom - g->cam0.zoom) * e;
            g->cam.camy = g->cam0.camy + (g->cam1.camy - g->cam0.camy) * e;
        }
        break;
    }
    case GsOver:
        break;
    }
}
