#include "../books_app.h"
#include <stdio.h>
#include <string.h>

enum {
    ItemContinue,
    ItemLibrary,
    ItemImport,
    ItemDelete,
    ItemSettings,
    ItemStats,
    ItemAbout,
};

/* Submenu only stores a pointer to the label, so the caller has to keep the
 * memory alive. We rebuild the strings on every enter so they're always in
 * sync with the current progress and book name. */
static char continue_label[80];

static void submenu_cb(void* ctx, uint32_t index) {
    BooksApp* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void build_continue_label(BooksApp* app) {
    /* Build "<title> (NN%)" style entry; falls back to file basename when
     * the title isn't loaded into app state. */
    const char* name = app->current_book_name;
    if(!name || !name[0]) {
        const char* slash = strrchr(app->current_book_path, '/');
        name = slash ? slash + 1 : app->current_book_path;
    }
    uint8_t pct = 0;
    if(app->progress.total_bytes > 0) {
        uint32_t p =
            (uint32_t)((uint64_t)app->progress.offset * 100u / app->progress.total_bytes);
        pct = p > 100 ? 100 : (uint8_t)p;
    }
    snprintf(continue_label, sizeof(continue_label),
             "Resume: %.30s (%u%%)", name, pct);
}

void books_scene_start_on_enter(void* ctx) {
    BooksApp* app = ctx;
    app->library_delete_mode = false;
    Submenu* m = app->submenu;
    submenu_reset(m);
    submenu_set_header(m, "Books");
    if(app->progress.offset > 0 && app->current_book_path[0] != '\0') {
        build_continue_label(app);
        submenu_add_item(m, continue_label, ItemContinue, submenu_cb, app);
    }
    submenu_add_item(m, "Library", ItemLibrary, submenu_cb, app);
    submenu_add_item(m, "Import / Rescan", ItemImport, submenu_cb, app);
    submenu_add_item(m, "Delete Book", ItemDelete, submenu_cb, app);
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
            app->library_delete_mode = false;
            scene_manager_next_scene(app->scene_manager, BooksSceneLibrary);
            return true;
        case ItemImport:
            scene_manager_next_scene(app->scene_manager, BooksSceneImport);
            return true;
        case ItemDelete:
            app->library_delete_mode = true;
            scene_manager_next_scene(app->scene_manager, BooksSceneLibrary);
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
