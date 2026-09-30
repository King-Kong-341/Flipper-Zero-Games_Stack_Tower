/*
 * Settings + statistics, stored as one small binary file on the SD card:
 *   /ext/apps_data/stack_tower/stack.sav
 * A magic number + version guard against old or damaged files; anything
 * unreadable silently falls back to the defaults.
 */
#include "stack.h"

#define SAVE_PATH APP_DATA_PATH("stack.sav")
#define SAVE_MAGIC 0x314B5453u /* "STK1" */
#define SAVE_VERSION 1

void store_defaults(SaveData* s) {
    memset(s, 0, sizeof(SaveData));
    s->magic = SAVE_MAGIC;
    s->version = SAVE_VERSION;
    s->sound = 1;
    s->volume = VolMid;
    s->vibro = 1;
    s->led = 1;
    s->theme = ThemeDay;
    s->intro = 1;
}

static void sanitize(SaveData* s) {
    if(s->volume >= VolCount) s->volume = VolMid;
    if(s->theme >= ThemeCount) s->theme = ThemeDay;
    s->sound = s->sound ? 1 : 0;
    s->vibro = s->vibro ? 1 : 0;
    s->led = s->led ? 1 : 0;
    s->intro = s->intro ? 1 : 0;
}

void store_load(App* app) {
    SaveData tmp;
    store_defaults(&app->save);
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t n = storage_file_read(file, &tmp, sizeof(tmp));
        if(n == sizeof(tmp) && tmp.magic == SAVE_MAGIC && tmp.version == SAVE_VERSION) {
            sanitize(&tmp);
            app->save = tmp;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
}

void store_save(App* app) {
    storage_common_mkdir(app->storage, APP_DATA_PATH(""));
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, SAVE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, &app->save, sizeof(SaveData));
    } else {
        FURI_LOG_W(TAG, "Could not write %s", SAVE_PATH);
    }
    storage_file_close(file);
    storage_file_free(file);
}

void store_reset_stats(App* app) {
    SaveData* s = &app->save;
    s->best = 0;
    s->games = 0;
    s->blocks = 0;
    s->perfects = 0;
    s->best_streak = 0;
    store_save(app);
}
