"""Renders animated GIFs of the intro and a full round for the docs.

    python sim_gifs.py      -> docs/screenshots/intro.gif, gameplay.gif
Uses the same mirror code as sim_screens.py, so every frame is pixel-exact.
"""
import os
from PIL import Image
import sim_screens as ss
from sim_core import *

TICK = 25  # ms, like FRAME_MS on the device
EVERY = 2  # keep every 2nd frame -> 20 fps GIF
SCALE = 3
ON, OFF, BORDER = (20, 20, 20), (255, 140, 0), (60, 60, 60)


def to_img(c):
    im = Image.new("RGB", (128, 64))
    im.putdata([ON if c.buf[y][x] else OFF for y in range(64) for x in range(128)])
    im = im.resize((128 * SCALE, 64 * SCALE), Image.NEAREST)
    out = Image.new("RGB", (128 * SCALE + 2 * SCALE, 64 * SCALE + 2 * SCALE), BORDER)
    out.paste(im, (SCALE, SCALE))
    return out


def write_gif(frames, name):
    os.makedirs(ss.DOCS, exist_ok=True)
    pal = [f.convert("P", palette=Image.ADAPTIVE, colors=4) for f in frames]
    path = os.path.join(ss.DOCS, name)
    pal[0].save(path, save_all=True, append_images=pal[1:], duration=TICK * EVERY, loop=0, optimize=True)
    print("wrote", path, len(frames), "frames", os.path.getsize(path) // 1024, "KB")


def intro_and_title():
    app = ss.App()
    app.save["best"] = 42
    app.demo.build_intro(ss.CAM_TITLE, ss.IN_SLABS)
    frames = []
    t = 0
    while t < ss.IN_END:
        if (t // TICK) % EVERY == 0:
            c = ss.C()
            app.now = t
            ss.intro_draw(app, c, t)
            frames.append(to_img(c))
        t += TICK
    app.demo.start_moving()
    end = t + 5500
    while t < end:
        app.demo.update(TICK)
        app.now = t
        if (t // TICK) % EVERY == 0:
            c = ss.C()
            ss.title_draw(app, c)
            frames.append(to_img(c))
        t += TICK
    write_gif(frames, "intro.gif")


def gameplay():
    app = ss.App()
    app.save.update(best=11, games=5)
    g = app.game
    g.reset(ss.CAM_GAME)
    # where the "player" presses OK: 0 = perfect, others slice, last one misses
    plan = [0.0, 0.0, 1.3, 0.0, -0.9, 0.0, 0.0, 0.0, 2.0, 0.0, -1.1, 0.0, 0.0, 14.0]
    frames = []
    t = 0
    step = 0
    end = None
    while True:
        prev = g.off
        g.update(TICK)
        app.screen_t += TICK
        app.now = t
        if g.phase == GS_PLAYING and g.moving and step < len(plan) and g.since_spawn >= 350:
            e = plan[step]
            if (prev - e) * (g.off - e) <= 0 and prev != g.off:
                g.off = e
                g.drop()
                if g.events & EV_MISS:
                    app.new_best = g.score > app.save["best"]
                    if app.new_best:
                        app.save["best"] = g.score
                g.events = 0
                step += 1
        if g.phase == GS_OVER and end is None:
            end = t + 2600
        if (t // TICK) % EVERY == 0:
            c = ss.C()
            ss.game_draw(app, c)
            frames.append(to_img(c))
        t += TICK
        if end is not None and t >= end:
            break
    write_gif(frames, "gameplay.gif")


if __name__ == "__main__":
    intro_and_title()
    gameplay()
