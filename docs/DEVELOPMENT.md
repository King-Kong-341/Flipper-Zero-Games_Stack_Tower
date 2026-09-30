# 🛠 Stack Tower — Developer Guide

How the app is built, how the isometric 3D renderer works, and how to tune
the game.

- [Build & install](#build--install)
- [Architecture](#architecture)
- [Main loop & timing](#main-loop--timing)
- [World & projection](#world--projection)
- [The renderer](#the-renderer)
- [Game rules in code](#game-rules-in-code)
- [Camera](#camera)
- [Autopilot](#autopilot)
- [Sound, vibration & LED](#sound-vibration--led)
- [Saved data](#saved-data)
- [Tuning the game](#tuning-the-game)
- [PC screen preview](#pc-screen-preview)
- [Continuous integration](#continuous-integration)

---

## Build & install

```bash
pip install ufbt
python -m ufbt              # -> dist/stack_tower.fap
python -m ufbt launch       # build + install + start on a USB-connected Flipper
```

Helper scripts in [`scripts/`](../scripts/):

| Script | Does |
|---|---|
| `build.ps1` / `build.sh` | builds and copies the `.fap` to the repo root |
| `install.ps1` / `install.sh` | builds, installs and launches on a connected Flipper |
| `preview.ps1` / `preview.sh` | renders all screens to PNG (see [PC screen preview](#pc-screen-preview)) |

The first `ufbt` run downloads the SDK and ARM toolchain (a few hundred MB) to `~/.ufbt`.
The app targets the **official release SDK** (built with 1.4.3).

## Architecture

One `ViewPort`, a hand-written screen state machine and one `App` struct
(`stack.h`). Three `Game` instances share the same rules code: the real
game, the title-screen autopilot and the how-to-play demos.

```
                 ┌───────────────┐
  input events ─▶│ stack_main.c  │── screens_input / update / draw ──┐
                 │ 40 fps loop   │                                   ▼
                 └──────┬────────┘                        ┌────────────────────┐
                        │ every wake-up                   │ stack_screens.c    │ intro, title, HUD,
                        ▼                                 │                    │ pause, game over, menus
              ┌──────────────────┐   events (EV_*)        └──┬──────────────┬──┘
              │ stack_fx.c       │◀──────────────────────────┘              │
              │ tones · vibro ·  │           game_update / game_drop        │ render_*
              │ LED sequencers   │                  ▼                       ▼
              └────────┬─────────┘        ┌────────────────┐     ┌─────────────────┐
                       │ HAL              │ stack_game.c   │────▶│ stack_render.c  │ framebuffer,
                       ▼                  │ rules, physics │     │ iso 3D, logo    │ canvas_draw_xbm
                   hardware               └────────────────┘     └─────────────────┘
   stack_store.c: SD card
```

| Module | Responsibility |
|---|---|
| `stack.h` | all constants (tuning!), types (`Slab`, `Cam`, `Piece`, `Ring`, `Game`, `SaveData`, `Fx`, `App`), prototypes |
| `stack_main.c` | lifecycle, main loop, mutex, input queue, backlight |
| `stack_game.c` | spawning, sliding, `game_drop()` (slice / perfect / grow / miss), pieces, rings, camera, zoom-out, autopilot |
| `stack_render.c` | 1-bit framebuffer, Bayer dithering, polygon fill, lines, projection, slab drawing, backgrounds, logo, fades |
| `stack_screens.c` | every screen's draw + input + update, intro timeline, transitions |
| `stack_fx.c` | tone / vibration / LED sequencers and all effect definitions |
| `stack_store.c` | binary save file with magic + version |

## Main loop & timing

```c
while(running) {
    wait = min(time to next frame, time to next sound/vibration step)
    got  = furi_message_queue_get(queue, &ev, wait);
    lock; dt = now - last;
    screens_update(app, dt);          // move everything to "now"
    if(got) screens_input(app, &ev);  // then react to the key
    fx_update(app);
    unlock;
    every 25 ms: view_port_update();  // 40 fps
}
```

The loop sleeps **on the input queue**, so a key press wakes it instantly.
The game is first advanced to the exact current time and *then* the drop
is handled — the slab lands where it was at the moment of the press, not
at the next frame. Drops react to `InputTypePress` (not release).

All movement is time based (`dt` in ms), so the speed is the same no matter
how fast the screen refreshes.

## World & projection

- **x, z** (ground plane) are in *iso grid units*: 1 unit = 2 px across and
  1 px down at zoom 1. The base slab is 11 × 11 units → a 44 × 22 px top face
  with perfectly clean 2:1 pixel-art edges.
- **y** (height) is in pixels at zoom 1. A slab is `LAYER_TH` = 4 px thick.

```c
screen_x = ox + (x - z) * 2 * zoom
screen_y = oy + ((x + z) - (y - camy)) * zoom
```

A `Cam` is `{ox, oy, zoom, camy}`. Different screens simply use different
cameras: the game (`64, 40, 1.0`), the title (`97, 31, 0.55`) and the help
pages (`96, 33, 0.5`).

## The renderer

The 3D scene is drawn into the app's own **128 × 64 framebuffer** (1 bit per
pixel, XBM layout) and blitted in one `canvas_draw_xbm()` call. Text and UI
are then drawn on top with the normal canvas API. Why an own buffer: the
public SDK has no access to the canvas buffer, and pattern fills need
per-pixel control.

- **Painter's algorithm:** back pieces → pillar → slabs bottom to top →
  perfect rings → moving slab → front pieces. Upper slabs always sit on
  lower ones, so no depth buffer is needed.
- **Slab** = 3 visible faces (top, left, right) filled as convex polygons
  (`fb_poly`, scanline, pixel centres on the edge count as inside) plus
  Bresenham edge lines. Day/Dots: top and left white, right black with a white
  highlight line between layers. Night: right face 9/16 dithered so it
  doesn't vanish into the black sky.
- **Dithering:** 4 × 4 Bayer matrix, levels 0 (white) … 16 (black). Used for
  faces, the Night style, dimming behind menus and all fade transitions.
- **Zoomed far out** (slab < 3 px thick) inner edges are skipped so the
  tower keeps its light and dark side.
- **Culling:** slabs completely below the screen are skipped; the pillar's
  bottom is computed so it always reaches just past the bottom edge.
- **Fades** (`render_fade`) paint Bayer dots in the background colour with
  `canvas_draw_xbm` in transparent bitmap mode, 4 rows at a time.

## Game rules in code

All in `game_drop()` (`stack_game.c`). The moving slab is the top slab
shifted by `off` along its axis (`axis` alternates every layer, `off`
ping-pongs between `-AMP` and `+AMP`).

```
|off| ≤ PERFECT_TOL      → perfect: snap to the slab below, combo++, ring
    combo ≥ GROW_COMBO   → grow both axes by GROW_AMOUNT (max BLOCK_S), animated
overlap < MIN_SIZE       → miss: the whole slab falls, game over
otherwise                → keep the overlap, the rest becomes a falling Piece
```

Falling pieces get gravity, a sideways drift away from the tower, and are
marked *front* or *back* (which side of the tower they fell off) for the
painter's order.

Speed: `SPEED_START + SPEED_STEP × score`, capped at `SPEED_MAX` (grid units / s).

Very long games: when the layer array (1000) is full, the lower half is
dropped and `first_index` keeps the heights correct.

## Camera

- **Playing:** `camy` eases towards the top of the tower (`k = 10/s`), so
  the newest slab stays in the same screen spot.
- **Game over:** after `FALL_MS`, `zoom_target()` computes a camera that fits
  the whole tower (top face + all layers + a bit of pillar) into 54 px, placed
  left of the result panel. `GsZoom` blends `ox, oy, zoom, camy` with an
  ease-in-out curve over `ZOOM_MS`.

## Autopilot

Title screen and help demos run a `Game` with `demo != DemoNone`. On every
spawn `demo_pick()` chooses where to "press": perfect (0) or a random offset,
depending on the mode. It drops when `off` crosses that value (after a short
"reaction time"). When the tower is full or too thin, it misses on purpose
and `demo_collapse()` turns every slab into a falling piece — the tower
crumbles, then the demo starts over.

## Sound, vibration & LED

`stack_fx.c` has three small non-blocking sequencers, driven from the main
loop (which also wakes up exactly when the next step is due):

| Sequencer | Step | HAL |
|---|---|---|
| tones | `{Hz, ms, volume %}` | `furi_hal_speaker_start/stop` |
| vibration | on/off durations | `furi_hal_vibro_on` |
| LED | `{r, g, b, ms, fade}` | `furi_hal_light_set` |

Perfect drops walk up a C-major pentatonic scale (C5 … E7), one step per
perfect in a row. The speaker is acquired on the first sound and released on
exit; if it's busy, the app retries at most every 2 s instead of blocking.
On exit the LED is handed back with `sequence_reset_rgb`.

## Saved data

`/ext/apps_data/stack_tower/stack.sav` — the `SaveData` struct as binary:
magic `STK1`, version, all settings, and the stats (best, games, blocks,
perfects, best streak). Unknown magic/version → defaults. **If you change
the struct, bump `SAVE_VERSION`** in `stack_store.c`.

Stats are written at the moment of the miss, settings when leaving the
settings screen.

## Tuning the game

Everything is in `stack.h`:

| Constant | Default | Effect |
|---|---|---|
| `SPEED_START` | 22 | slide speed at the start (grid units / s; one pass ≈ 1.5 s) |
| `SPEED_STEP` | 0.30 | extra speed per placed slab |
| `SPEED_MAX` | 40 | top speed |
| `PERFECT_TOL` | 0.8 | how far off a drop may be and still count as perfect (≈ ±36 ms at the start) |
| `GROW_COMBO` | 8 | perfects in a row before the slab grows |
| `GROW_AMOUNT` | 1.0 | growth per axis per perfect |
| `AMP` | 16.5 | how far the slab slides out |
| `LAYER_TH` | 4 | slab thickness in px |
| `BLOCK_S` | 11 | size of the base slab (11 gives crisp 2:1 pixel edges) |

## PC screen preview

There is no host C compiler needed: [`tools/flipper_preview`](../tools/flipper_preview/)
contains a Python mirror of the renderer, rules and screens.

```bash
pip install pillow
python tools/flipper_preview/sim_screens.py          # -> tools/flipper_preview/shots/
python tools/flipper_preview/sim_screens.py --docs   # also refresh docs/screenshots/
python tools/flipper_preview/sim_gifs.py             # docs/screenshots/intro.gif + gameplay.gif
```

The fonts are extracted from your local ufbt SDK on the first run.
**Keep constants and coordinates in sync with the C code** when you change
layouts (`sim_core.py` ↔ `stack_game.c` / `stack_render.c`,
`sim_screens.py` ↔ `stack_screens.c`).

## Continuous integration

`.github/workflows/build.yml` builds the app on every push with
[flipperzero-ufbt-action](https://github.com/flipperdevices/flipperzero-ufbt-action)
against the release SDK and uploads the `.fap` as an artifact.
