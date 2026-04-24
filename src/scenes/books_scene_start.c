#include "../books_app.h"

enum { ItemContinue, ItemLibrary, ItemImport, ItemSettings, ItemStats, ItemAbout };

static void submenu_cb(void* ctx, uint32_t index) {
    BooksApp* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void books_scene_start_on_enter(void* ctx) {
    BooksApp* app = ctx;
    Submenu* m = app->submenu;
    submenu_reset(m);
    submenu_set_header(m, "Books");
    if(app->progress.offset > 0 && app->current_book_path[0] != '\0') {
        submenu_add_item(m, "Continue Reading", ItemContinue, submenu_cb, app);
    }
    submenu_add_item(m, "Library", ItemLibrary, submenu_cb, app);
    submenu_add_item(m, "Import / Rescan", ItemImport, submenu_cb, app);
    submenu_add_item(m, "Settings", ItemSettings, submenu_cb, app);
    submenu_add_item(m, "Reading Stats", ItemStats, submenu_cb, app);
    submenu_add_item(m, "About", ItemAbout, submenu_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewSubmenu);
}

bool books_scene_start_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case ItemContinue:
            scene_manager_next_scene(app->scene_manager, BooksSceneReader);
            return true;
        case ItemLibrary:
            scene_manager_next_scene(app->scene_manager, BooksSceneLibrary);
            return true;
        case ItemImport:
            scene_manager_next_scene(app->scene_manager, BooksSceneImport);
            return true;
        case ItemSettings:
            scene_manager_next_scene(app->scene_manager, BooksSceneSettings);
            return true;
        case ItemStats:
            scene_manager_next_scene(app->scene_manager, BooksSceneStats);
            return true;
        case ItemAbout:
            scene_manager_next_scene(app->scene_manager, BooksSceneAbout);
            return true;
        }
    }
    return false;
}

void books_scene_start_on_exit(void* ctx) {
    BooksApp* app = ctx;
    submenu_reset(app->submenu);
}
