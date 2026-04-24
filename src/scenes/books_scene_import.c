#include "../books_app.h"
#include <stdio.h>

void books_scene_import_on_enter(void* ctx) {
    BooksApp* app = ctx;
    Popup* p = app->popup;
    popup_reset(p);
    popup_set_header(p, "Scanning...", 64, 8, AlignCenter, AlignTop);

    char paths[32][256];
    uint16_t n = fbook_scan_library(paths, 32);

    static char summary[64];
    snprintf(summary, sizeof(summary), "Found %u books\nin library + cache", n);
    popup_set_text(p, summary, 64, 28, AlignCenter, AlignTop);
    popup_set_timeout(p, 1500);
    popup_enable_timeout(p);
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewPopup);
}

bool books_scene_import_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void books_scene_import_on_exit(void* ctx) {
    BooksApp* app = ctx;
    popup_reset(app->popup);
}
