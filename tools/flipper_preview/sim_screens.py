"""Python mirror of stack_screens.c drawing code -> PNG screenshots.

    python sim_screens.py        writes shots/*.png and shots/_all.png
Keep coordinates in sync with stack_screens.c.
"""
import os
import math
import flipsim as fs
from sim_core import *

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(HERE, "shots")

CAM_GAME = Cam(64.0, 40.0, 1.0)
CAM_TITLE = Cam(97.0, 31.0, 0.55)
CAM_HELP = Cam(96.0, 33.0, 0.5)

IN_PILLAR_END = 400
IN_SLAB0 = 400
IN_SLAB_GAP = 260
IN_SLAB_DUR = 300
IN_SLABS = 4
IN_LOGO0 = 1500
IN_LETTER_GAP = 90
IN_LETTER_DUR = 300
IN_TEXT0 = 2000
IN_TEXT_DUR = 300
IN_BAR0 = 2250
IN_BAR_DUR = 300
IN_END = 2750

ICON_GEAR = [0x054, 0x0FE, 0x07C, 0x1C7, 0x0C6, 0x1C7, 0x07C, 0x0FE, 0x054]
ICON_TROPHY = [0x1FF, 0x17D, 0x17D, 0x0FE, 0x07C, 0x038, 0x010, 0x038, 0x07C]
ICON_HELP = [0x07C, 0x0C6, 0x006, 0x00C, 0x018, 0x018, 0x000, 0x018, 0x018]
ICON_PLAY = [0x8, 0xC, 0xE, 0xF, 0xE, 0xC, 0x8]
ICON_BACK = [0x20, 0x60, 0xFE, 0x61, 0x21, 0x01, 0x1E]

BLACK, WHITE = 1, 0
FONTS = {"FontPrimary": "primary", "FontSecondary": "secondary", "FontBigNumbers": "big"}


class C(fs.Canvas):
    """Canvas with the Flipper API names used by the C code."""

    def setf(self, name):
        self.set_font(FONTS[name])

    def sw(self, s):
        return self.str_width(s)

    def aligned(self, x, y, h, v, s):
        if h == "right":
            x -= self.font_w(s)
        elif h == "center":
            x -= self.font_w(s) // 2
        if v == "top":
            y += self.font.ascent_A
        elif v == "center":
            y += self.font.ascent_A // 2
        self.str(x, y, s)

    def font_w(self, s):
        return self.font.width(s)


class App:
    def __init__(self, theme=THEME_DAY):
        self.save = dict(sound=1, volume=1, vibro=1, led=1, theme=theme, intro=1,
                         best=0, games=0, blocks=0, perfects=0, best_streak=0)
        self.fb = FB()
        self.now = 0
        self.screen_t = 0
        self.title_sel = 0
        self.paused = False
        self.pause_sel = 0
        self.set_sel = 0
        self.set_top = 0
        self.confirm = False
        self.confirm_sel = 0
        self.stat_top = 0
        self.help_page = 0
        self.new_best = False
        self.game = Game(MAIN_CAP, DEMO_NONE, 99)
        self.demo = Game(DEMO_CAP, DEMO_TITLE, 7)
        self.help = Game(HELP_CAP, DEMO_HELP_DROP, 5)


def fg(app):
    return WHITE if app.save["theme"] == THEME_NIGHT else BLACK


def bg(app):
    return BLACK if app.save["theme"] == THEME_NIGHT else WHITE


def clamp01(v):
    return max(0.0, min(1.0, v))


def ease_out_cubic(t):
    t = clamp01(t)
    return 1 - (1 - t) ** 3


def ease_out_back(t):
    t = clamp01(t)
    c1 = 1.70158
    c3 = c1 + 1
    return 1 + c3 * (t - 1) ** 3 + c1 * (t - 1) ** 2


def ease_out_bounce(t):
    t = clamp01(t)
    n1, d1 = 7.5625, 2.75
    if t < 1 / d1:
        return n1 * t * t
    if t < 2 / d1:
        t -= 1.5 / d1
        return n1 * t * t + 0.75
    if t < 2.5 / d1:
        t -= 2.25 / d1
        return n1 * t * t + 0.9375
    t -= 2.625 / d1
    return n1 * t * t + 0.984375


def draw_icon(c, x, y, rows, w, h):
    for r in range(h):
        for col in range(w):
            if rows[r] & (1 << (w - 1 - col)):
                c.dot(x + col, y + r)


def halo_text(c, x, y, h, v, s, f, b):
    c.color = b
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx or dy:
                c.aligned(x + dx, y + dy, h, v, s)
    c.color = f
    c.aligned(x, y, h, v, s)


def key_hint(c, x, y, key, label, f, b):
    c.setf("FontSecondary")
    kw = c.sw(key) + 5
    c.color = f
    c.rbox(x, y, kw, 9, 2)
    c.color = b
    c.aligned(x + kw // 2 + 1, y + 1, "center", "top", key)
    c.color = f
    c.aligned(x + kw + 3, y + 1, "left", "top", label)
    return kw + 3 + c.sw(label)


def back_hint(c, x, y, label, f):
    c.color = f
    draw_icon(c, x, y + 1, ICON_BACK, 8, 7)
    c.setf("FontSecondary")
    c.aligned(x + 11, y + 1, "left", "top", label)


def panel(c, x, y, w, h, f, b):
    c.color = b
    c.rbox(x, y, w, h, 3)
    c.color = f
    c.rframe(x, y, w, h, 3)


def scrollbar(c, x, y, h, pos, total, visible, f):
    if total <= visible:
        return
    c.color = f
    for yy in range(y, y + h, 2):
        c.dot(x + 1, yy)
    th = max(4, h * visible // total)
    ty = y + (h - th) * pos // (total - visible)
    c.box(x, ty, 3, th)


def draw_title_texts(app, c, bar_y, reveal):
    f, b = fg(app), bg(app)
    c.color = f
    c.setf("FontSecondary")
    tower = "TOWER"
    widths = [c.sw(ch) for ch in tower]
    total = sum(widths) + 3 * 4
    x = 32 - total // 2
    for i, ch in enumerate(tower):
        c.str(x, 29, ch)
        x += widths[i] + 3
    c.line(4, 26, 32 - total // 2 - 5, 26)
    c.line(32 + total // 2 + 4, 26, 60, 26)
    buf = "BEST %d" % app.save["best"]
    c.setf("FontPrimary")
    w = c.sw(buf) + 12
    bx = 32 - w // 2
    draw_icon(c, bx, 34, ICON_TROPHY, 9, 9)
    c.str(bx + 12, 43, buf)
    if reveal < 1:
        render_fade(c, 0, 20, 64, 26, int(16 * (1 - reveal)), b == BLACK)
    if bar_y < 64:
        c.color = b
        c.box(0, bar_y - 1, 128, 64 - bar_y + 1)
        btn = [(1, 58), (62, 21), (85, 21), (108, 19)]
        for i in range(4):
            sel = app.title_sel == i
            bxx, bw = btn[i]
            by, bh = bar_y, 13
            c.color = f
            if sel:
                c.rbox(bxx, by, bw, bh, 3)
            else:
                c.rframe(bxx, by, bw, bh, 3)
            c.color = b if sel else f
            cx = bxx + bw // 2
            if i == 0:
                c.setf("FontPrimary")
                tw = c.sw("PLAY")
                sx = cx - (tw + 8) // 2
                draw_icon(c, sx, by + 3, ICON_PLAY, 4, 7)
                c.str(sx + 8, by + 10, "PLAY")
            else:
                ic = ICON_GEAR if i == 1 else (ICON_TROPHY if i == 2 else ICON_HELP)
                draw_icon(c, cx - 4, by + 2, ic, 9, 9)


def intro_draw(app, c, t):
    th = app.save["theme"]
    g = app.demo
    render_background(app.fb, th, g.cam.camy, app.now)
    pillar = -(1 - ease_out_cubic(t / IN_PILLAR_END)) * 70
    yoff = [0.0] * (1 + IN_SLABS)
    shown = 1
    for k in range(IN_SLABS):
        st = t - (IN_SLAB0 + k * IN_SLAB_GAP)
        if st < 0:
            break
        shown = k + 2
        yoff[k + 1] = (1 - ease_out_bounce(st / IN_SLAB_DUR)) * 46
    render_scene(app.fb, g, th, False, shown, yoff, pillar)
    dy = []
    for l in range(5):
        st = t - (IN_LOGO0 + l * IN_LETTER_GAP)
        dy.append(-40.0 if st < 0 else -24 * (1 - ease_out_back(st / IN_LETTER_DUR)))
    render_logo(app.fb, 3, 4, th, dy)
    render_blit(c, app.fb)
    reveal = clamp01((t - IN_TEXT0) / IN_TEXT_DUR)
    bar_y = 51 + int((1 - ease_out_cubic((t - IN_BAR0) / IN_BAR_DUR)) * 14)
    draw_title_texts(app, c, bar_y, reveal)


def title_draw(app, c):
    th = app.save["theme"]
    render_background(app.fb, th, app.demo.cam.camy, app.now)
    render_scene(app.fb, app.demo, th, True)
    render_logo(app.fb, 3, 4, th)
    render_blit(c, app.fb)
    draw_title_texts(app, c, 51, 1.0)


def draw_score(app, c, g):
    buf = "%d" % g.score
    dy = 0
    if g.pop_t < POP_MS:
        dy = -int(3 * (1 - g.pop_t / POP_MS) + 0.5)
    c.setf("FontBigNumbers")
    halo_text(c, 64, 17 + dy, "center", "bottom", buf, fg(app), bg(app))


def draw_pause(app, c):
    f, b = fg(app), bg(app)
    render_fade(c, 0, 0, 128, 64, 8, b == BLACK)
    panel(c, 26, 5, 76, 54, f, b)
    c.color = f
    c.setf("FontPrimary")
    c.aligned(64, 15, "center", "bottom", "PAUSED")
    c.line(34, 18, 94, 18)
    c.setf("FontSecondary")
    for i, it in enumerate(["Resume", "Restart", "Menu"]):
        y = 21 + i * 12
        if app.pause_sel == i:
            c.color = f
            c.rbox(31, y, 66, 11, 2)
            c.color = b
        else:
            c.color = f
        c.aligned(64, y + 2, "center", "top", it)


def draw_game_over(app, c, g):
    f, b = fg(app), bg(app)
    k = ease_out_cubic(g.phase_t / PANEL_MS)
    x = 75 + int((1 - k) * 56)
    panel(c, x, 1, 52, 62, f, b)
    cx = x + 26
    c.color = f
    c.setf("FontSecondary")
    c.aligned(cx, 3, "center", "top", "SCORE")
    c.setf("FontBigNumbers")
    c.aligned(cx, 27, "center", "bottom", "%d" % g.score)
    c.setf("FontSecondary")
    if app.new_best:
        on = ((g.phase_t // 350) % 4) != 3
        if on:
            c.rbox(x + 2, 30, 48, 11, 2)
            c.color = b
        c.aligned(cx + 1, 32, "center", "top", "NEW BEST")
        c.color = f
    else:
        c.aligned(cx, 32, "center", "top", "BEST %d" % app.save["best"])
    c.line(x + 5, 43, x + 46, 43)
    key_hint(c, x + 5, 45, "OK", "Retry", f, b)
    back_hint(c, x + 5, 53, "Menu", f)


def game_draw(app, c):
    g = app.game
    th = app.save["theme"]
    render_background(app.fb, th, g.cam.camy, app.now)
    render_scene(app.fb, g, th, True)
    render_blit(c, app.fb)
    if g.phase in (GS_PLAYING, GS_FALLING):
        draw_score(app, c, g)
    if g.phase == GS_PLAYING and g.score == 0 and app.save["games"] < 3 and not app.paused:
        if (app.screen_t // 500) % 3 != 2:
            c.setf("FontSecondary")
            w = c.sw("OK") + 5 + 3 + c.sw("to drop")
            panel(c, 64 - w // 2 - 4, 51, w + 8, 12, fg(app), bg(app))
            key_hint(c, 64 - w // 2, 52, "OK", "to drop", fg(app), bg(app))
    if g.phase == GS_OVER:
        draw_game_over(app, c, g)
    if app.paused:
        draw_pause(app, c)


LIST_Y, LIST_ROW, LIST_VISIBLE = 14, 12, 4


def draw_header(app, c, icon, title):
    c.color = fg(app)
    draw_icon(c, 3, 1, icon, 9, 9)
    c.setf("FontPrimary")
    c.str(16, 10, title)
    c.line(0, 12, 127, 12)


def draw_list_row(app, c, row, sel, label, value, arrows):
    f, b = fg(app), bg(app)
    y = LIST_Y + row * LIST_ROW
    c.setf("FontSecondary")
    if sel:
        c.color = f
        c.rbox(1, y, 121, LIST_ROW - 1, 2)
        c.color = b
    else:
        c.color = f
    c.aligned(5, y + 2, "left", "top", label)
    if value:
        if arrows:
            vw = c.sw(value)
            c.aligned(117, y + 2, "right", "top", ">")
            c.aligned(111, y + 2, "right", "top", value)
            c.aligned(111 - vw - 3, y + 2, "right", "top", "<")
        else:
            c.aligned(117, y + 2, "right", "top", value)


SET_LABELS = ["Sound", "Volume", "Vibration", "LED", "Background", "Intro animation", "Reset stats"]


def setting_value(app, i):
    s = app.save
    oo = lambda v: "On" if v else "Off"
    return [oo(s["sound"]), ["Low", "Medium", "High"][s["volume"]], oo(s["vibro"]), oo(s["led"]),
            ["Day", "Dots", "Night"][s["theme"]], oo(s["intro"])][i]


def settings_draw(app, c):
    f, b = fg(app), bg(app)
    if b == BLACK:
        c.color = BLACK
        c.box(0, 0, 128, 64)
    draw_header(app, c, ICON_GEAR, "SETTINGS")
    for r in range(LIST_VISIBLE):
        i = app.set_top + r
        if i >= 7:
            break
        sel = app.set_sel == i
        if i == 6:
            draw_list_row(app, c, r, sel, SET_LABELS[i], "OK" if sel else "...", False)
        else:
            draw_list_row(app, c, r, sel, SET_LABELS[i], setting_value(app, i), sel)
    scrollbar(c, 124, LIST_Y, LIST_VISIBLE * LIST_ROW - 1, app.set_top, 7, LIST_VISIBLE, f)
    if app.confirm:
        render_fade(c, 0, 0, 128, 64, 8, b == BLACK)
        panel(c, 12, 9, 104, 46, f, b)
        c.color = f
        c.setf("FontPrimary")
        c.aligned(64, 19, "center", "bottom", "Reset stats?")
        c.setf("FontSecondary")
        c.aligned(64, 22, "center", "top", "Best score and totals")
        for i, o in enumerate(["No", "Yes"]):
            x = 22 + i * 44
            sel = app.confirm_sel == i
            c.color = f
            if sel:
                c.rbox(x, 37, 40, 13, 3)
            else:
                c.rframe(x, 37, 40, 13, 3)
            c.color = b if sel else f
            c.setf("FontPrimary")
            c.aligned(x + 20, 47, "center", "bottom", o)


STAT_LABELS = ["Best score", "Games played", "Slabs stacked", "Perfect drops", "Best perfect streak",
               "Average score", "Perfect rate"]


def stats_draw(app, c):
    s = app.save
    f, b = fg(app), bg(app)
    if b == BLACK:
        c.color = BLACK
        c.box(0, 0, 128, 64)
    draw_header(app, c, ICON_TROPHY, "STATS")
    for r in range(LIST_VISIBLE):
        i = app.stat_top + r
        if i >= 7:
            break
        if i == 5:
            avg10 = s["blocks"] * 10 // s["games"] if s["games"] else 0
            val = "%d.%d" % (avg10 // 10, avg10 % 10)
        elif i == 6:
            val = "%d%%" % (s["perfects"] * 100 // s["blocks"] if s["blocks"] else 0)
        else:
            val = "%d" % [s["best"], s["games"], s["blocks"], s["perfects"], s["best_streak"]][i]
        draw_list_row(app, c, r, False, STAT_LABELS[i], val, False)
        c.color = f
        if r < LIST_VISIBLE - 1 and i < 6:
            y = LIST_Y + r * LIST_ROW + LIST_ROW - 1
            for x in range(5, 118, 2):
                c.dot(x, y)
    scrollbar(c, 124, LIST_Y, LIST_VISIBLE * LIST_ROW - 1, app.stat_top, 7, LIST_VISIBLE, f)


HELP_TITLES = ["DROP", "SLICE", "PERFECT", "ABOUT"]
HELP_LINES = [
    ["Press OK to drop", "the sliding slab", "onto the tower.", "Stack it high!"],
    ["Whatever hangs", "over the edge is", "sliced off. Miss", "and it's over."],
    ["Land it exactly:", "no loss at all.", "8 perfects in a", "row: it grows!"],
]


def help_draw(app, c):
    th = app.save["theme"]
    f, b = fg(app), bg(app)
    p = app.help_page
    render_background(app.fb, th, app.help.cam.camy, app.now)
    if p < 3:
        render_scene(app.fb, app.help, th, True)
    else:
        render_logo(app.fb, 35, 5, th)
    render_blit(c, app.fb)
    c.color = f
    if p < 3:
        c.color = b
        c.box(0, 0, 66, 53)
        c.color = f
        c.setf("FontPrimary")
        c.str(3, 10, HELP_TITLES[p])
        c.line(3, 12, 3 + c.sw(HELP_TITLES[p]), 12)
        c.setf("FontSecondary")
        for i in range(4):
            c.str(3, 22 + i * 9, HELP_LINES[p][i])
    else:
        c.setf("FontSecondary")
        c.aligned(64, 26, "center", "top", "TOWER  v1.0")
        c.aligned(64, 35, "center", "top", "made by Toni")
        c.aligned(64, 44, "center", "top", "inspired by Stack")
    c.color = b
    c.box(0, 54, 128, 10)
    c.color = f
    x0 = 64 - (4 * 7 - 3) // 2
    for i in range(4):
        if i == p:
            c.rbox(x0 + i * 7, 57, 4, 4, 1)
        else:
            c.rframe(x0 + i * 7, 57, 4, 4, 1)
    c.setf("FontSecondary")
    if p > 0:
        c.str(2, 62, "<")
    if p < 3:
        c.aligned(125, 62, "right", "bottom", ">")


# ================= scenarios =================

def run(g, ms, step=FRAME_MS):
    t = 0
    while t < ms:
        g.update(step)
        t += step


def drop_at(g, off):
    """Move the slab to `off` (as if the player pressed there) and drop."""
    g.off = off
    g.drop()


DOCS = os.path.normpath(os.path.join(HERE, "..", "..", "docs", "screenshots"))
# preview name -> file name in docs/screenshots (written with --docs)
DOC_NAMES = {
    "intro_1700": "intro", "title": "title", "title_night": "title_night", "title_dots": "title_dots",
    "game_first": "first_drop", "game_mid": "gameplay", "game_perfect": "perfect", "game_slice": "slice",
    "game_miss": "miss", "game_zoom": "zoom_out", "game_over_best": "game_over_new_best",
    "game_over_tall": "game_over", "game_pause": "pause", "game_night": "theme_night",
    "game_dots": "theme_dots", "settings": "settings", "settings_confirm": "reset_stats",
    "settings_night": "settings_night", "stats": "stats", "help_0": "help_drop", "help_1": "help_slice",
    "help_2": "help_perfect", "help_3": "about",
}
WRITE_DOCS = False


def save(c, name):
    os.makedirs(SHOTS, exist_ok=True)
    c.save(os.path.join(SHOTS, name + ".png"), scale=4)
    if WRITE_DOCS and name in DOC_NAMES:
        os.makedirs(DOCS, exist_ok=True)
        c.save(os.path.join(DOCS, DOC_NAMES[name] + ".png"), scale=3)


def contact_sheet(names, out, cols=3):
    from PIL import Image
    ims = [Image.open(os.path.join(SHOTS, n + ".png")) for n in names]
    W, H = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (W + 8), rows * (H + 8)), (30, 30, 30))
    for i, im in enumerate(ims):
        sheet.paste(im, ((i % cols) * (W + 8), (i // cols) * (H + 8)))
    sheet.save(os.path.join(SHOTS, out + ".png"))


def played_game(app, n, errs, theme_seed=1):
    g = app.game
    g.reset(CAM_GAME)
    for i in range(n):
        run(g, 200)
        drop_at(g, errs[i % len(errs)])
    return g


def main():
    shots = []

    def shot(name, fn, theme=THEME_DAY, **kw):
        app = App(theme)
        c = C()
        fn(app, c, **kw)
        save(c, name)
        shots.append(name)

    # --- intro frames ---
    def intro(app, c, t):
        app.demo.build_intro(CAM_TITLE, IN_SLABS)
        app.now = t
        intro_draw(app, c, t)

    for t in (250, 800, 1300, 1700, 2150, 2450):
        shot("intro_%04d" % t, intro, t=t)

    # --- title ---
    def title(app, c, sel=0, ms=2400):
        app.save["best"] = 128
        app.title_sel = sel
        app.demo.build_intro(CAM_TITLE, IN_SLABS)
        app.demo.start_moving()
        run(app.demo, ms)
        title_draw(app, c)

    shot("title", title)
    shot("title_sel_gear", title, sel=1, ms=5000)
    shot("title_night", title, theme=THEME_NIGHT)
    shot("title_dots", title, theme=THEME_DOTS, ms=9000)

    # --- game ---
    errs = [0.0, 1.2, 0.0, -0.9, 0.0, 0.0, 1.6, -1.1]

    def game_first(app, c):
        app.game.reset(CAM_GAME)
        run(app.game, 300)
        app.screen_t = 100
        game_draw(app, c)

    def game_mid(app, c, n=12, ms=500, theme_night=False):
        g = played_game(app, n, errs)
        run(g, ms)
        game_draw(app, c)

    def game_perfect(app, c):
        g = played_game(app, 9, errs)
        run(g, 150)
        drop_at(g, 0.2)
        run(g, 120)
        game_draw(app, c)

    def game_slice(app, c):
        g = played_game(app, 9, errs)
        run(g, 150)
        drop_at(g, 2.4)
        run(g, 150)
        game_draw(app, c)

    def game_miss(app, c, ms=300):
        g = played_game(app, 14, errs)
        run(g, 150)
        drop_at(g, 12.0)
        app.save["games"] = 5
        run(g, ms)
        game_draw(app, c)

    def game_over(app, c, n=24, best=True):
        g = played_game(app, n, errs)
        run(g, 150)
        drop_at(g, -13.0)
        app.new_best = best
        app.save["best"] = 57 if not best else n
        app.save["games"] = 5
        run(g, FALL_MS + ZOOM_MS + 400)
        game_draw(app, c)

    def game_over_tall(app, c):
        game_over(app, c, n=90, best=False)

    def game_pause(app, c):
        g = played_game(app, 7, errs)
        run(g, 300)
        app.paused = True
        app.pause_sel = 1
        game_draw(app, c)

    shot("game_first", game_first)
    shot("game_mid", game_mid)
    shot("game_perfect", game_perfect)
    shot("game_slice", game_slice)
    shot("game_miss", game_miss)
    shot("game_zoom", game_miss, ms=FALL_MS + ZOOM_MS // 2)
    shot("game_over_best", game_over)
    shot("game_over_tall", game_over_tall)
    shot("game_pause", game_pause)
    shot("game_night", game_mid, theme=THEME_NIGHT)
    shot("game_dots", game_mid, theme=THEME_DOTS)
    shot("game_over_night", game_over, theme=THEME_NIGHT)

    # --- menus ---
    def settings(app, c, sel=1, top=0, confirm=False):
        app.set_sel, app.set_top, app.confirm = sel, top, confirm
        settings_draw(app, c)

    def stats(app, c, top=0):
        app.save.update(best=128, games=42, blocks=1234, perfects=321, best_streak=17)
        app.stat_top = top
        stats_draw(app, c)

    shot("settings", settings)
    shot("settings_scrolled", settings, sel=6, top=3)
    shot("settings_confirm", settings, sel=6, top=3, confirm=True)
    shot("stats", stats)
    shot("stats_scrolled", stats, top=3)
    shot("settings_night", settings, theme=THEME_NIGHT, sel=4, top=1)

    def helpp(app, c, page=0, ms=2500):
        app.help_page = page
        modes = [DEMO_HELP_DROP, DEMO_HELP_SLICE, DEMO_HELP_PERFECT, DEMO_NONE]
        app.help.demo = modes[page]
        app.help.reset(CAM_HELP)
        if modes[page] == DEMO_NONE:
            app.help.moving = False
        run(app.help, ms)
        help_draw(app, c)

    for p in range(4):
        shot("help_%d" % p, helpp, page=p, ms=[2500, 4200, 9000, 100][p])

    contact_sheet([s for s in shots if s.startswith("intro")] + ["title", "title_sel_gear", "title_night",
                                                                 "title_dots"], "_sheet_intro_title")
    contact_sheet([s for s in shots if s.startswith("game")], "_sheet_game")
    contact_sheet([s for s in shots if s.startswith(("settings", "stats", "help"))], "_sheet_menus")
    print("wrote", len(shots), "screens")


if __name__ == "__main__":
    import sys
    WRITE_DOCS = "--docs" in sys.argv
    main()
