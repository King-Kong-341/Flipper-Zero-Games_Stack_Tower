/*
 * App entry point: setup, main loop, cleanup.
 *
 * The main loop runs the game at a fixed 40 fps. It sleeps on the input
 * queue, so key presses are handled the moment they arrive (important for
 * a timing game), and wakes up early whenever a sound/vibration step is
 * due. All state is guarded by one mutex shared with the draw callback.
 */
#include "stack.h"

static void render_callback(Canvas* canvas, void* ctx) {
    App* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    screens_draw(app, canvas);
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* ev, void* ctx) {
    App* app = ctx;
    furi_message_queue_put(app->queue, ev, 0);
}

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(16, sizeof(InputEvent));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notif = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);

    store_load(app);
    fx_init(app);
    game_init(&app->game, MAIN_CAP, DemoNone);
    game_init(&app->demo, DEMO_CAP, DemoTitle);
    game_init(&app->help, HELP_CAP, DemoHelpDrop);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, render_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    notification_message(app->notif, &sequence_display_backlight_enforce_on);
    app->running = true;
    return app;
}

static void app_free(App* app) {
    fx_deinit(app);
    notification_message(app->notif, &sequence_reset_rgb);
    notification_message(app->notif, &sequence_display_backlight_enforce_auto);

    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);

    game_free(&app->game);
    game_free(&app->demo);
    game_free(&app->help);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
}

int32_t stack_tower_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->now = furi_get_tick();
    screens_start(app);
    furi_mutex_release(app->mutex);

    uint32_t last = furi_get_tick();
    uint32_t next_frame = last;

    while(app->running) {
        uint32_t now = furi_get_tick();
        int32_t wait = (int32_t)(next_frame - now);
        uint32_t fx_due = fx_next_due(app);
        if(wait < 0) wait = 0;
        if((uint32_t)wait > fx_due) wait = (int32_t)fx_due;

        InputEvent ev;
        bool got = furi_message_queue_get(app->queue, &ev, (uint32_t)wait) == FuriStatusOk;

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        now = furi_get_tick();
        uint32_t dt = now - last;
        if(dt > 100) dt = 100; /* e.g. after a long SD write */
        last = now;
        app->now = now;
        screens_update(app, dt);
        if(got) screens_input(app, &ev);
        fx_update(app);
        furi_mutex_release(app->mutex);

        if((int32_t)(now - next_frame) >= 0) {
            view_port_update(app->view_port);
            next_frame += FRAME_MS;
            if((int32_t)(now - next_frame) > FRAME_MS) next_frame = now + FRAME_MS;
        }
    }

    app_free(app);
    return 0;
}
