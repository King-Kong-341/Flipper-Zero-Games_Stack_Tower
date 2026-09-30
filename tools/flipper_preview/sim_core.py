"""Python mirror of stack_game.c + stack_render.c (keep constants and
algorithms in sync with the C code). Used to render pixel-exact previews."""
import math

# ---- constants (stack.h) ----
FRAME_MS = 25
FADE_MS = 220
BLOCK_S = 11.0
LAYER_TH = 4.0
AMP = 16.5
SPEED_START = 22.0
SPEED_STEP = 0.30
SPEED_MAX = 40.0
PERFECT_TOL = 0.8
MIN_SIZE = 0.3
GROW_COMBO = 8
GROW_AMOUNT = 1.0
GRAVITY = 380.0
PILLAR_VIS = 10.0
RING_MS = 480
GROW_MS = 180
POP_MS = 140
FALL_MS = 800
ZOOM_MS = 1200
PANEL_MS = 260
COLLAPSE_MS = 1100
DEMO_WAIT_MS = 380
MAIN_CAP = 1000
DEMO_CAP = 16
HELP_CAP = 14
MAX_PIECES = 20
MAX_RINGS = 4

GS_PLAYING, GS_FALLING, GS_ZOOM, GS_OVER, GS_COLLAPSE = range(5)
DEMO_NONE, DEMO_TITLE, DEMO_HELP_DROP, DEMO_HELP_SLICE, DEMO_HELP_PERFECT = range(5)
EV_PLACE, EV_PERFECT, EV_GROW, EV_MISS = 1, 2, 4, 8
THEME_DAY, THEME_DOTS, THEME_NIGHT = range(3)


class Slab:
    __slots__ = ("x0", "x1", "z0", "z1")

    def __init__(self, x0, x1, z0, z1):
        self.x0, self.x1, self.z0, self.z1 = x0, x1, z0, z1

    def copy(self):
        return Slab(self.x0, self.x1, self.z0, self.z1)


class Cam:
    def __init__(self, ox, oy, zoom, camy=0.0):
        self.ox, self.oy, self.zoom, self.camy = ox, oy, zoom, camy

    def copy(self):
        return Cam(self.ox, self.oy, self.zoom, self.camy)


class Piece:
    def __init__(self):
        self.active = False


class Ring:
    def __init__(self):
        self.active = False
        self.age = 0


def ease_in_out(t):
    return 4 * t * t * t if t < 0.5 else 1 - pow(-2 * t + 2, 3) / 2


class Game:
    def __init__(self, cap, demo, seed=12345):
        self.cap = cap
        self.demo = demo
        self.rng = seed & 0xFFFFFFFF
        self.layers = []
        self.first_index = 0
        self.pieces = [Piece() for _ in range(MAX_PIECES)]
        self.rings = [Ring() for _ in range(MAX_RINGS)]
        self.events = 0

    def frand(self):
        self.rng = (self.rng * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.rng >> 8) / 16777216.0

    @property
    def count(self):
        return len(self.layers)

    def top_y(self):
        return (self.first_index + self.count - 1) * LAYER_TH

    # ---- helpers ----
    def add_piece(self, s, y, axis, front, vd, vy):
        for p in self.pieces:
            if p.active:
                continue
            p.s, p.y, p.vy, p.vd, p.axis, p.front, p.active = s.copy(), y, vy, vd, axis, front, True
            return

    def add_ring(self, s, y):
        slot, oldest = 0, 0
        for i, r in enumerate(self.rings):
            if not r.active:
                slot = i
                break
            if r.age >= oldest:
                oldest, slot = r.age, i
        r = self.rings[slot]
        r.s, r.y, r.age, r.active = s.copy(), y, 0, True

    def demo_pick(self):
        t = self.layers[-1]
        small = (t.x1 - t.x0 < 4) or (t.z1 - t.z0 < 4)
        full = self.count >= self.cap - 1
        sign = -1.0 if self.frand() < 0.5 else 1.0
        r = self.frand()
        if full or (small and self.demo != DEMO_HELP_PERFECT):
            self.demo_err = sign * (AMP - 1)
            return
        if self.demo == DEMO_TITLE:
            self.demo_err = 0.0 if r < 0.6 else sign * (0.9 + self.frand() * 1.3)
        elif self.demo == DEMO_HELP_DROP:
            self.demo_err = 0.0 if r < 0.75 else sign * (0.9 + self.frand() * 0.6)
        elif self.demo == DEMO_HELP_SLICE:
            self.demo_err = sign * (1.4 + self.frand() * 1.2)
        elif self.demo == DEMO_HELP_PERFECT:
            self.demo_err = sign * 1.6 if self.score < 2 else 0.0
        else:
            self.demo_err = 0.0

    def demo_collapse(self):
        base = self.first_index * LAYER_TH
        for i in range(1, self.count):
            axis = 0 if self.frand() < 0.5 else 1
            vd = 1.0 + self.frand() * 4.0
            vy = self.frand() * 50
            self.add_piece(self.layers[i], base + (i - 1) * LAYER_TH, axis, True, vd, vy)
        del self.layers[1:]
        self.phase = GS_COLLAPSE
        self.phase_t = 0

    def spawn(self):
        self.cur = self.layers[-1].copy()
        self.axis = (self.first_index + self.count - 1) & 1
        self.off = -AMP
        self.dir = 1.0
        self.speed = min(SPEED_MAX, SPEED_START + SPEED_STEP * self.score)
        self.moving = True
        self.since_spawn = 0
        if self.demo:
            self.demo_pick()

    def reset(self, layout):
        h = BLOCK_S / 2
        self.layers = [Slab(-h, h, -h, h)]
        self.first_index = 0
        self.score = self.combo = self.max_combo = self.perfects = 0
        self.phase = GS_PLAYING
        self.phase_t = 0
        self.growing = False
        self.grow_t = 0
        self.pop_t = POP_MS
        self.events = 0
        self.pieces = [Piece() for _ in range(MAX_PIECES)]
        self.rings = [Ring() for _ in range(MAX_RINGS)]
        self.base = layout.copy()
        self.cam = layout.copy()
        self.cam.camy = 0.0
        self.spawn()

    def build_intro(self, layout, slabs):
        self.reset(layout)
        self.moving = False
        for _ in range(slabs):
            self.layers.append(self.layers[0].copy())
        self.cam.camy = self.top_y()

    def start_moving(self):
        if not self.moving and self.phase == GS_PLAYING:
            self.spawn()

    def drop(self):
        if not self.moving or self.phase != GS_PLAYING:
            return
        top = self.layers[-1]
        a = self.axis
        off = self.off
        y = self.top_y()
        lp = top.z0 if a else top.x0
        hp = top.z1 if a else top.x1
        placed = top.copy()
        self.moving = False
        if abs(off) <= PERFECT_TOL:
            self.combo += 1
            self.perfects += 1
            self.max_combo = max(self.max_combo, self.combo)
            self.events |= EV_PERFECT
            if self.combo >= GROW_COMBO:
                cx = (placed.x0 + placed.x1) / 2
                cz = (placed.z0 + placed.z1) / 2
                wx = min(BLOCK_S, placed.x1 - placed.x0 + GROW_AMOUNT)
                wz = min(BLOCK_S, placed.z1 - placed.z0 + GROW_AMOUNT)
                if wx > placed.x1 - placed.x0 + 0.01 or wz > placed.z1 - placed.z0 + 0.01:
                    self.grow_from = placed.copy()
                    placed = Slab(cx - wx / 2, cx + wx / 2, cz - wz / 2, cz + wz / 2)
                    self.growing = True
                    self.grow_t = 0
                    self.events |= EV_GROW
            self.add_ring(placed, y + LAYER_TH)
        else:
            lc, hc = lp + off, hp + off
            lo, hi = max(lc, lp), min(hc, hp)
            self.combo = 0
            if hi - lo < MIN_SIZE:
                miss = top.copy()
                setax(miss, a, lc, hc)
                self.add_piece(miss, y, a, off > 0, 2.5 if off > 0 else -2.5, 0.0)
                self.events |= EV_MISS
                self.phase_t = 0
                if self.demo:
                    self.demo_collapse()
                else:
                    self.phase = GS_FALLING
                return
            setax(placed, a, lo, hi)
            cut = top.copy()
            if off > 0:
                setax(cut, a, hi, hc)
                self.add_piece(cut, y, a, True, 2.0, 0.0)
            else:
                setax(cut, a, lc, lo)
                self.add_piece(cut, y, a, False, -2.0, 0.0)
            self.events |= EV_PLACE
        if self.count >= self.cap:
            d = self.cap // 2
            del self.layers[:d]
            self.first_index += d
        self.layers.append(placed)
        self.score += 1
        self.pop_t = 0
        self.spawn()

    def zoom_target(self):
        top = self.top_y()
        base = self.first_index * LAYER_TH
        hw = (top - base) + PILLAR_VIS + BLOCK_S * 2
        z = min(1.0, 54.0 / hw)
        h = hw * z
        margin = (64 - h) / 2
        return Cam(38.0, margin + BLOCK_S * z, z, top)

    def skip_zoom(self):
        if self.phase == GS_FALLING:
            self.cam1 = self.zoom_target()
        if self.phase in (GS_FALLING, GS_ZOOM):
            self.cam = self.cam1.copy()
            self.phase = GS_OVER
            self.phase_t = 0

    def update(self, dt):
        s = dt / 1000.0
        self.phase_t += dt
        self.since_spawn += dt
        if self.pop_t < POP_MS:
            self.pop_t += dt
        if self.moving and self.phase == GS_PLAYING:
            prev = self.off
            self.off += self.dir * self.speed * s
            if self.off > AMP:
                self.off = 2 * AMP - self.off
                self.dir = -1.0
            elif self.off < -AMP:
                self.off = -2 * AMP - self.off
                self.dir = 1.0
            if self.demo and self.since_spawn >= DEMO_WAIT_MS:
                e = self.demo_err
                if (prev - e) * (self.off - e) <= 0 and prev != self.off:
                    self.off = e
                    self.drop()
        for p in self.pieces:
            if not p.active:
                continue
            p.vy += GRAVITY * s
            p.y -= p.vy * s
            d = p.vd * s
            if p.axis:
                p.s.z0 += d
                p.s.z1 += d
            else:
                p.s.x0 += d
                p.s.x1 += d
            sy = self.cam.oy + ((p.s.x0 + p.s.z0) - (p.y + LAYER_TH - self.cam.camy)) * self.cam.zoom
            if sy > 72:
                p.active = False
        for r in self.rings:
            if not r.active:
                continue
            r.age += dt
            if r.age >= RING_MS:
                r.active = False
        if self.growing:
            self.grow_t += dt
            if self.grow_t >= GROW_MS:
                self.growing = False
        if self.phase in (GS_PLAYING, GS_FALLING, GS_COLLAPSE):
            target = self.top_y()
            k = min(1.0, s * 10)
            self.cam.camy += (target - self.cam.camy) * k
            if self.phase == GS_FALLING and self.phase_t >= FALL_MS:
                self.cam0 = self.cam.copy()
                self.cam1 = self.zoom_target()
                self.phase = GS_ZOOM
                self.phase_t = 0
            elif self.phase == GS_COLLAPSE and self.phase_t >= COLLAPSE_MS:
                self.reset(self.base)
        elif self.phase == GS_ZOOM:
            t = self.phase_t / ZOOM_MS
            if t >= 1:
                self.cam = self.cam1.copy()
                self.phase = GS_OVER
                self.phase_t = 0
            else:
                e = ease_in_out(t)
                c0, c1 = self.cam0, self.cam1
                self.cam.ox = c0.ox + (c1.ox - c0.ox) * e
                self.cam.oy = c0.oy + (c1.oy - c0.oy) * e
                self.cam.zoom = c0.zoom + (c1.zoom - c0.zoom) * e
                self.cam.camy = c0.camy + (c1.camy - c0.camy) * e


def setax(s, a, lo, hi):
    if a:
        s.z0, s.z1 = lo, hi
    else:
        s.x0, s.x1 = lo, hi


# ================= renderer (stack_render.c) =================

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def style_for(theme):
    if theme == THEME_NIGHT:
        return dict(top=0, left=0, right=9, ink=True, rsep=False)
    return dict(top=0, left=0, right=16, ink=True, rsep=True)


class FB:
    def __init__(self):
        self.b = bytearray(1024)

    def px(self, x, y, black):
        if not (0 <= x < 128 and 0 <= y < 64):
            return
        i = y * 16 + (x >> 3)
        m = 1 << (x & 7)
        if black:
            self.b[i] |= m
        else:
            self.b[i] &= ~m & 0xFF

    def get(self, x, y):
        return (self.b[y * 16 + (x >> 3)] >> (x & 7)) & 1

    def clear(self, black):
        v = 0xFF if black else 0
        for i in range(1024):
            self.b[i] = v

    def span(self, x0, x1, y, lvl):
        if y < 0 or y > 63:
            return
        x0 = max(0, x0)
        x1 = min(127, x1)
        row = BAYER[y & 3]
        for x in range(x0, x1 + 1):
            self.px(x, y, row[x & 3] < lvl)

    def rect_pattern(self, x, y, w, h, lvl):
        for j in range(h):
            self.span(x, x + w - 1, y + j, lvl)

    def line(self, x0, y0, x1, y1, black, dash=0):
        if (x0 < 0 and x1 < 0) or (x0 > 127 and x1 > 127) or (y0 < 0 and y1 < 0) or (y0 > 63 and y1 > 63):
            return
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        n = 0
        while True:
            if not dash or n % dash == 0:
                self.px(x0, y0, black)
            n += 1
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def poly(self, p, lvl):
        ymin = max(0, min(q[1] for q in p))
        ymax = min(63, max(q[1] for q in p))
        n = len(p)
        for y in range(ymin, ymax + 1):
            xl, xr = 1e9, -1e9
            for i in range(n):
                ax, ay = p[i]
                bx, by = p[(i + 1) % n]
                if (ay <= y <= by) or (by <= y <= ay):
                    if ay == by:
                        xl = min(xl, ax, bx)
                        xr = max(xr, ax, bx)
                    else:
                        x = ax + (y - ay) * (bx - ax) / (by - ay)
                        xl = min(xl, x)
                        xr = max(xr, x)
            if xl <= xr:
                self.span(math.ceil(xl - 1e-4), math.floor(xr + 1e-4), y, lvl)


def proj(c, x, z, y):
    sx = c.ox + (x - z) * 2 * c.zoom
    sy = c.oy + ((x + z) - (y - c.camy)) * c.zoom
    sx = max(-20000, min(20000, sx))
    sy = max(-20000, min(20000, sy))
    return (int(math.floor(sx + 0.5)), int(math.floor(sy + 0.5)))


def draw_slab(fb, c, s, ybot, ytop, st):
    t00 = proj(c, s.x0, s.z0, ytop)
    t10 = proj(c, s.x1, s.z0, ytop)
    t11 = proj(c, s.x1, s.z1, ytop)
    t01 = proj(c, s.x0, s.z1, ytop)
    b10 = proj(c, s.x1, s.z0, ybot)
    b11 = proj(c, s.x1, s.z1, ybot)
    b01 = proj(c, s.x0, s.z1, ybot)
    fb.poly([t10, t11, b11, b10], st["right"])
    fb.poly([t01, t11, b11, b01], st["left"])
    fb.poly([t00, t10, t11, t01], st["top"])
    k = st["ink"]
    for a, b in [(t00, t10), (t10, t11), (t11, t01), (t01, t00), (t01, b01), (t10, b10)]:
        fb.line(a[0], a[1], b[0], b[1], k)
    thin = (b11[1] - t11[1]) < 3
    if not thin:
        fb.line(t11[0], t11[1], b11[0], b11[1], k)
        fb.line(b01[0], b01[1], b11[0], b11[1], k)
        fb.line(b11[0], b11[1], b10[0], b10[1], k)
        if st["rsep"] and b10[0] - b11[0] >= 2:
            fb.line(b11[0] + 1, b11[1] - 1, b10[0], b10[1] - 1, False)


def draw_ring(fb, c, r, ink):
    t = r.age / RING_MS
    e = 1 - (1 - t) ** 3
    m = 0.4 + 3.0 * e
    a = proj(c, r.s.x0 - m, r.s.z0 - m, r.y)
    b = proj(c, r.s.x1 + m, r.s.z0 - m, r.y)
    d = proj(c, r.s.x1 + m, r.s.z1 + m, r.y)
    f = proj(c, r.s.x0 - m, r.s.z1 + m, r.y)
    dash = 0 if t < 0.45 else (2 if t < 0.75 else 3)
    for p, q in [(a, b), (b, d), (d, f), (f, a)]:
        fb.line(p[0], p[1], q[0], q[1], ink, dash)


def u32(v):
    return v & 0xFFFFFFFF


def hash32(v):
    v = u32((v + 1) * 2654435761)
    v ^= v >> 13
    v = u32(v * 0x5BD1E995)
    v ^= v >> 15
    return v


def c_int(f):
    """C float->int conversion (truncation toward zero)."""
    return int(f)


def pmod(a, m):
    r = int(math.fmod(a, m))
    return r + m if r < 0 else r


def c_div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def render_background(fb, theme, camy, t):
    if theme == THEME_NIGHT:
        fb.clear(True)
        shift = c_int(camy * 0.3)
        for i in range(42):
            h = hash32(i)
            x = h % 128
            y = pmod((h >> 8) % 96 + shift, 96)
            if y >= 64:
                continue
            if ((t // 170) + (h >> 16)) % 13 == 0:
                continue
            fb.px(x, y, False)
            if ((h >> 20) & 7) == 0:
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    fb.px(x + dx, y + dy, False)
        return
    fb.clear(False)
    if theme == THEME_DOTS:
        shift = c_int(camy * 0.5)
        for y in range(64):
            ry = y - shift
            if pmod(ry, 8) != 0:
                continue
            row = c_div(ry - pmod(ry, 8), 8)
            ox = 8 if (row & 1) else 0
            for x in range(ox, 128, 16):
                fb.px(x, y, True)


def lerp_slab(a, b, t):
    return Slab(a.x0 + (b.x0 - a.x0) * t, a.x1 + (b.x1 - a.x1) * t, a.z0 + (b.z0 - a.z0) * t, a.z1 + (b.z1 - a.z1) * t)


def draw_pieces(fb, g, front, st):
    for p in g.pieces:
        if not p.active or p.front != front:
            continue
        draw_slab(fb, g.cam, p.s, p.y, p.y + LAYER_TH, st)


def render_scene(fb, g, theme, show_cur, draw_layers=-1, yoff=None, pillar_off=0.0):
    c = g.cam
    st = style_for(theme)
    base = g.first_index * LAYER_TH
    draw_pieces(fb, g, False, st)
    ptop = base + pillar_off
    pbot = c.camy + BLOCK_S - (72 - c.oy) / c.zoom
    if pbot < ptop - 1:
        draw_slab(fb, c, g.layers[0], pbot, ptop, st)
    n = g.count
    if 0 <= draw_layers < n:
        n = draw_layers
    for i in range(1, n):
        yb = base + (i - 1) * LAYER_TH
        if yoff:
            yb += yoff[i]
        yt = yb + LAYER_TH
        s = g.layers[i]
        if i == g.count - 1 and g.growing:
            t = g.grow_t / GROW_MS
            e = 1 - (1 - t) ** 2
            s = lerp_slab(g.grow_from, g.layers[i], e)
        top_vy = c.oy + ((s.x0 + s.z0) - (yt - c.camy)) * c.zoom
        bot_vy = c.oy + ((s.x1 + s.z1) - (yb - c.camy)) * c.zoom
        if top_vy > 66 or bot_vy < -2:
            continue
        draw_slab(fb, c, s, yb, yt, st)
    for r in g.rings:
        if r.active:
            draw_ring(fb, c, r, theme != THEME_NIGHT)
    if show_cur and g.moving and g.phase == GS_PLAYING:
        s = g.cur.copy()
        if g.axis:
            s.z0 += g.off
            s.z1 += g.off
        else:
            s.x0 += g.off
            s.x1 += g.off
        y = g.top_y()
        draw_slab(fb, c, s, y, y + LAYER_TH, st)
    draw_pieces(fb, g, True, st)


LOGO = [
    [0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E],
    [0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04],
    [0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],
    [0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F],
    [0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11],
]


def render_logo(fb, x, y, theme, letter_dy=None):
    ink = theme != THEME_NIGHT
    for l in range(5):
        lx = x + l * 12
        ly = y + (int(math.floor(letter_dy[l] + 0.5)) if letter_dy else 0)
        for r in range(7):
            for col in range(5):
                if not (LOGO[l][r] & (0x10 >> col)):
                    continue
                for py in range(2):
                    for pxx in range(2):
                        X = lx + col * 2 + pxx + 1
                        Y = ly + r * 2 + py + 1
                        if ((X + Y) & 1) == 0:
                            fb.px(X, Y, ink)
        for r in range(7):
            for col in range(5):
                if not (LOGO[l][r] & (0x10 >> col)):
                    continue
                for py in range(2):
                    for pxx in range(2):
                        fb.px(lx + col * 2 + pxx, ly + r * 2 + py, ink)


def render_blit(canvas, fb):
    for y in range(64):
        for x in range(128):
            if fb.get(x, y):
                canvas.buf[y][x] = 1


def render_fade(canvas, x, y, w, h, lvl, black):
    if lvl == 0 or w <= 0 or h <= 0:
        return
    w = min(w, 128)
    col = 1 if black else 0
    for j in range(h):
        for i in range(w):
            if BAYER[(y + j) & 3][(x + i) & 3] < lvl:
                canvas.px(x + i, y + j, col)
