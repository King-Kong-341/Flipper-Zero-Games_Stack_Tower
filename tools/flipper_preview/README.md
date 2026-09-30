# Flipper Preview (PC)

Zeigt die Stack-Tower-Bildschirme am PC mit den **echten** Flipper-Schriften
(werden beim ersten Start automatisch aus dem ufbt-SDK geholt).
Nur eine Vorschau zum Pixel-Prüfen - ersetzt nicht den Test am Gerät.

- `flipsim.py`     - Canvas wie auf dem Flipper (u8g2-Schriften, Rahmen, Kreise, Linien)
- `sim_core.py`    - Nachbau von `stack_game.c` + `stack_render.c` (Regeln, 3D-Renderer)
- `sim_screens.py` - Nachbau von `stack_screens.c` -> Bilder in `shots/`

    python sim_screens.py

Konstanten und Koordinaten müssen mit dem C-Code synchron bleiben.
