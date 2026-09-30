/*
 * Sound, vibration and RGB LED effects.
 *
 * Three tiny non-blocking sequencers driven from the main loop:
 *   - tones: piezo notes with per-note volume (perfect drops climb a
 *     pentatonic scale, just like the original)
 *   - vibration: alternating on/off durations
 *   - LED: colour steps, optionally fading out
 * Each one respects its own switch in the settings.
 */
#include "stack.h"

/* C major pentatonic, climbing with every perfect drop in a row. */
static const uint16_t SCALE[] = {
    523, 587, 659, 784, 880, 1047, 1175, 1319, 1568, 1760, 2093, 2349, 2637};
#define SCALE_LEN (sizeof(SCALE) / sizeof(SCALE[0]))

static float volume_gain(uint8_t v) {
    switch(v) {
    case VolLow:
        return 0.25f;
    case VolHigh:
        return 1.0f;
    default:
        return 0.55f;
    }
}

/* ---------- Tones ---------- */

static void tone_next(App* app, uint32_t now) {
    Fx* fx = &app->fx;
    if(fx->tone_i >= fx->tone_n) {
        if(fx->speaker) furi_hal_speaker_stop();
        fx->tone_busy = false;
        return;
    }
    const Tone* t = &fx->tones[fx->tone_i++];
    if(fx->speaker) {
        if(t->f) {
            float vol = volume_gain(app->save.volume) * (float)t->vol / 100.0f;
            furi_hal_speaker_start((float)t->f, vol);
        } else {
            furi_hal_speaker_stop();
        }
    }
    fx->tone_end = now + t->ms;
    fx->tone_busy = true;
}

static void play_tones(App* app, const Tone* tones, uint8_t n) {
    if(!app->save.sound) return;
    Fx* fx = &app->fx;
    if(!fx->speaker) {
        /* speaker busy (e.g. another sound playing)? retry at most every 2 s
         * so the game loop never stalls on it */
        uint32_t now = furi_get_tick();
        if(fx->speaker_retry && (int32_t)(now - fx->speaker_retry) < 0) return;
        fx->speaker = furi_hal_speaker_acquire(30);
        if(!fx->speaker) {
            fx->speaker_retry = now + 2000;
            return;
        }
    }
    if(n > FX_MAX_TONES) n = FX_MAX_TONES;
    memcpy(fx->tones, tones, sizeof(Tone) * n);
    fx->tone_n = n;
    fx->tone_i = 0;
    tone_next(app, furi_get_tick());
}

/* ---------- Vibration ---------- */

static void vib_next(App* app, uint32_t now) {
    Fx* fx = &app->fx;
    if(fx->vib_i >= fx->vib_n) {
        furi_hal_vibro_on(false);
        fx->vib_busy = false;
        return;
    }
    bool on = (fx->vib_i % 2) == 0;
    furi_hal_vibro_on(on);
    fx->vib_end = now + fx->vib[fx->vib_i++];
    fx->vib_busy = true;
}

static void play_vib(App* app, const uint16_t* pattern, uint8_t n) {
    if(!app->save.vibro) return;
    Fx* fx = &app->fx;
    if(n > FX_MAX_VIB) n = FX_MAX_VIB;
    memcpy(fx->vib, pattern, sizeof(uint16_t) * n);
    fx->vib_n = n;
    fx->vib_i = 0;
    vib_next(app, furi_get_tick());
}

/* ---------- LED ---------- */

static void led_set(uint8_t r, uint8_t g, uint8_t b) {
    furi_hal_light_set(LightRed, r);
    furi_hal_light_set(LightGreen, g);
    furi_hal_light_set(LightBlue, b);
}

static void play_led(App* app, const LedStep* steps, uint8_t n) {
    if(!app->save.led) return;
    Fx* fx = &app->fx;
    if(n > FX_MAX_LED) n = FX_MAX_LED;
    memcpy(fx->led, steps, sizeof(LedStep) * n);
    fx->led_n = n;
    fx->led_i = 0;
    fx->led_start = furi_get_tick();
    fx->led_busy = true;
    led_set(steps[0].r, steps[0].g, steps[0].b);
}

static void led_update(App* app, uint32_t now) {
    Fx* fx = &app->fx;
    if(!fx->led_busy) return;
    while(fx->led_i < fx->led_n) {
        const LedStep* s = &fx->led[fx->led_i];
        uint32_t el = now - fx->led_start;
        if(el < s->ms) {
            float k = s->fade ? 1.0f - (float)el / (float)s->ms : 1.0f;
            led_set((uint8_t)(s->r * k), (uint8_t)(s->g * k), (uint8_t)(s->b * k));
            return;
        }
        fx->led_start += s->ms;
        fx->led_i++;
    }
    led_set(0, 0, 0);
    fx->led_busy = false;
}

/* ---------- Engine ---------- */

void fx_init(App* app) {
    memset(&app->fx, 0, sizeof(Fx));
}

void fx_stop_all(App* app) {
    Fx* fx = &app->fx;
    if(fx->speaker) furi_hal_speaker_stop();
    fx->tone_busy = false;
    furi_hal_vibro_on(false);
    fx->vib_busy = false;
    if(fx->led_busy) led_set(0, 0, 0);
    fx->led_busy = false;
}

void fx_deinit(App* app) {
    fx_stop_all(app);
    if(app->fx.speaker) {
        furi_hal_speaker_release();
        app->fx.speaker = false;
    }
    led_set(0, 0, 0);
}

void fx_update(App* app) {
    Fx* fx = &app->fx;
    uint32_t now = furi_get_tick();
    if(fx->tone_busy && (int32_t)(now - fx->tone_end) >= 0) tone_next(app, now);
    if(fx->vib_busy && (int32_t)(now - fx->vib_end) >= 0) vib_next(app, now);
    led_update(app, now);
}

/* Next moment a sequencer needs attention (for the main loop timeout). */
uint32_t fx_next_due(App* app) {
    Fx* fx = &app->fx;
    int32_t due = INT32_MAX;
    uint32_t now = furi_get_tick();
    if(fx->tone_busy) {
        int32_t d = (int32_t)(fx->tone_end - now);
        if(d < due) due = d;
    }
    if(fx->vib_busy) {
        int32_t d = (int32_t)(fx->vib_end - now);
        if(d < due) due = d;
    }
    return due < 0 ? 0 : (uint32_t)due;
}

/* ---------- Game sounds ---------- */

void fx_place(App* app) {
    static const Tone t[] = {{900, 5, 45}, {360, 22, 80}, {240, 18, 45}};
    static const uint16_t v[] = {16};
    static const LedStep l[] = {{50, 50, 50, 90, true}};
    play_tones(app, t, 3);
    play_vib(app, v, 1);
    play_led(app, l, 1);
}

void fx_perfect(App* app, uint16_t combo) {
    uint16_t i = combo ? combo - 1 : 0;
    if(i >= SCALE_LEN) i = SCALE_LEN - 1;
    uint16_t f = SCALE[i];
    Tone t[] = {{f, 70, 100}, {f, 60, 35}, {f, 60, 12}};
    static const uint16_t v[] = {28};
    uint16_t b = 110 + combo * 20;
    if(b > 255) b = 255;
    LedStep l[] = {{0, (uint8_t)(b / 3), (uint8_t)b, 240, true}};
    play_tones(app, t, 3);
    play_vib(app, v, 1);
    play_led(app, l, 1);
}

void fx_grow(App* app, uint16_t combo) {
    uint16_t i = combo ? combo - 1 : 0;
    if(i >= SCALE_LEN) i = SCALE_LEN - 1;
    uint16_t f = SCALE[i];
    if(f > 1400) f = 1047; /* keep the arpeggio inside the piezo's sweet spot */
    Tone t[] = {
        {f, 50, 100},
        {(uint16_t)(f * 5 / 4), 50, 100},
        {(uint16_t)(f * 3 / 2), 50, 100},
        {(uint16_t)(f * 2), 110, 90},
        {(uint16_t)(f * 2), 70, 30}};
    static const uint16_t v[] = {30, 60, 30};
    static const LedStep l[] = {{0, 255, 255, 380, true}};
    play_tones(app, t, 5);
    play_vib(app, v, 3);
    play_led(app, l, 1);
}

void fx_miss(App* app) {
    static const Tone t[] = {
        {523, 70, 90}, {392, 70, 85}, {311, 80, 80}, {233, 90, 75}, {175, 220, 70}};
    static const uint16_t v[] = {260};
    static const LedStep l[] = {{255, 0, 0, 550, true}};
    play_tones(app, t, 5);
    play_vib(app, v, 1);
    play_led(app, l, 1);
}

void fx_result(App* app, bool new_best) {
    if(new_best) {
        static const Tone t[] = {
            {1047, 80, 90}, {1319, 80, 90}, {1568, 80, 90}, {2093, 240, 90}, {2093, 120, 30}};
        static const uint16_t v[] = {50, 70, 50, 70, 90};
        static const LedStep l[] = {
            {0, 255, 0, 150, false},
            {0, 0, 0, 90, false},
            {0, 255, 0, 150, false},
            {0, 0, 0, 90, false},
            {0, 255, 0, 450, true}};
        play_tones(app, t, 5);
        play_vib(app, v, 5);
        play_led(app, l, 5);
    } else {
        static const Tone t[] = {{659, 60, 60}, {523, 140, 50}};
        play_tones(app, t, 2);
    }
}

/* ---------- UI sounds ---------- */

void fx_nav(App* app) {
    static const Tone t[] = {{1600, 7, 35}};
    play_tones(app, t, 1);
}

void fx_select(App* app) {
    static const Tone t[] = {{1047, 25, 70}, {1568, 45, 70}};
    static const uint16_t v[] = {14};
    play_tones(app, t, 2);
    play_vib(app, v, 1);
}

void fx_back(App* app) {
    static const Tone t[] = {{1568, 25, 60}, {1047, 45, 60}};
    play_tones(app, t, 2);
}

void fx_toggle(App* app, bool on) {
    static const Tone t_on[] = {{1319, 30, 70}, {1760, 50, 70}};
    static const Tone t_off[] = {{1760, 30, 70}, {1319, 50, 70}};
    play_tones(app, on ? t_on : t_off, 2);
}

void fx_pause(App* app) {
    static const Tone t[] = {{880, 40, 60}, {659, 70, 60}};
    play_tones(app, t, 2);
}

void fx_intro_land(App* app, uint8_t k) {
    static const uint16_t notes[] = {523, 659, 784, 1047};
    Tone t[] = {{notes[k & 3], 55, 75}, {notes[k & 3], 50, 25}};
    static const uint16_t v[] = {14};
    static const LedStep l[] = {{70, 70, 70, 140, true}};
    play_tones(app, t, 2);
    play_vib(app, v, 1);
    play_led(app, l, 1);
}

void fx_intro_logo(App* app) {
    static const Tone t[] = {
        {523, 30, 50}, {659, 30, 55}, {784, 30, 60}, {1047, 30, 65}, {1319, 30, 70},
        {1568, 110, 75}, {1568, 80, 25}};
    static const LedStep l[] = {{60, 120, 255, 420, true}};
    play_tones(app, t, 7);
    play_led(app, l, 1);
}

void fx_test_vibro(App* app) {
    static const uint16_t v[] = {90};
    play_vib(app, v, 1);
}

void fx_test_led(App* app) {
    static const LedStep l[] = {{0, 120, 255, 450, true}};
    play_led(app, l, 1);
}

void fx_test_volume(App* app) {
    static const Tone t[] = {{1047, 90, 100}, {1568, 120, 100}};
    play_tones(app, t, 2);
}
