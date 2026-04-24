#include "../books_app.h"

#define LIBRARY_MAX 64

typedef struct {
    char paths[LIBRARY_MAX][256];
    uint16_t count;
} LibraryState;

static LibraryState library_state;

static void submenu_cb(void* ctx, uint32_t index) {
    BooksApp* app = ctx;
    if(index >= library_state.count) return;
    strncpy(app->current_book_path, library_state.paths[index], sizeof(app->current_book_path) - 1);
    app->current_book_path[sizeof(app->current_book_path) - 1] = '\0';
    const char* slash = strrchr(app->current_book_path, '/');
    strncpy(
        app->current_book_name,
        slash ? slash + 1 : app->current_book_path,
        sizeof(app->current_book_name) - 1);
    app->current_book_name[sizeof(app->current_book_name) - 1] = '\0';
    view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventOpenBook);
}

void books_scene_library_on_enter(void* ctx) {
    BooksApp* app = ctx;
    Submenu* m = app->submenu;
    submenu_reset(m);
    submenu_set_header(m, "Library");

    library_state.count = fbook_scan_library(library_state.paths, LIBRARY_MAX);
    if(library_state.count == 0) {
        submenu_add_item(m, "(no books found)", 0xFFFF, NULL, app);
        submenu_add_item(m, "Import / Rescan", 0xFFFE, NULL, app);
    } else {
        for(uint16_t i = 0; i < library_state.count; ++i) {
            const char* slash = strrchr(library_state.paths[i], '/');
            const char* name = slash ? slash + 1 : library_state.paths[i];
            submenu_add_item(m, name, i, submenu_cb, app);
        }
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewSubmenu);
}

bool books_scene_library_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type == SceneManagerEventTypeCustom && event.event == BooksEventOpenBook) {
        scene_manager_next_scene(app->scene_manager, BooksSceneReader);
        return true;
    }
    return false;
}

void books_scene_library_on_exit(void* ctx) {
    BooksApp* app = ctx;
    submenu_reset(app->submenu);
}
