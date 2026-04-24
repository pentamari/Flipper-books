#include "../books_app.h"
#include <string.h>

#define LIBRARY_MAX 64

/* Static so we don't blow the app stack on rescan. */
static char scan_paths[LIBRARY_MAX][256];

static void on_select(uint16_t index, void* ctx) {
    BooksApp* app = ctx;
    (void)index;
    const char* path = library_view_selected_path(app->library);
    if(!path) return;
    const char* slash = strrchr(path, '/');
    const char* name = slash ? slash + 1 : path;

    if(app->library_delete_mode) {
        strncpy(app->pending_delete_path, path, sizeof(app->pending_delete_path) - 1);
        app->pending_delete_path[sizeof(app->pending_delete_path) - 1] = '\0';
        strncpy(app->pending_delete_name, name, sizeof(app->pending_delete_name) - 1);
        app->pending_delete_name[sizeof(app->pending_delete_name) - 1] = '\0';
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventConfirmDelete);
    } else {
        strncpy(app->current_book_path, path, sizeof(app->current_book_path) - 1);
        app->current_book_path[sizeof(app->current_book_path) - 1] = '\0';
        strncpy(app->current_book_name, name, sizeof(app->current_book_name) - 1);
        app->current_book_name[sizeof(app->current_book_name) - 1] = '\0';
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventOpenBook);
    }
}

void books_scene_library_on_enter(void* ctx) {
    BooksApp* app = ctx;
    library_view_reset(app->library,
                       app->library_delete_mode ? "Delete which?" : "Library",
                       app->library_delete_mode);
    library_view_set_select_callback(app->library, on_select, app);

    uint16_t count = fbook_scan_library(scan_paths, LIBRARY_MAX);
    for(uint16_t i = 0; i < count; ++i) {
        const char* path = scan_paths[i];
        char title[64] = {0};
        char author[48] = {0};
        uint32_t text_length = 0;
        uint32_t word_count = 0;
        if(!fbook_peek(path, title, sizeof(title), author, sizeof(author),
                       &text_length, &word_count)) {
            /* Couldn't read header - fall back to filename so the user can
             * still see (and delete) it. */
            const char* slash = strrchr(path, '/');
            const char* name = slash ? slash + 1 : path;
            strncpy(title, name, sizeof(title) - 1);
            title[sizeof(title) - 1] = '\0';
            text_length = 0;
        }

        uint32_t off = 0, total = text_length, last_read = 0;
        uint8_t fav = 0;
        book_progress_load_summary(path, &off, &total, &last_read, &fav);
        uint8_t pct = 0;
        if(total > 0) {
            uint32_t p = off * 100u / total;
            pct = p > 100 ? 100 : (uint8_t)p;
        }

        uint16_t cw = 0, ch = 0;
        uint8_t fmt = 0;
        uint8_t* cov = fbook_peek_cover(path, &cw, &ch, &fmt);

        library_view_add_entry(app->library, path, title, pct, last_read,
                               fav != 0, cov, cw, ch);
    }

    library_view_apply_sort(app->library, app->settings.library_sort);
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewLibrary);
}

bool books_scene_library_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BooksEventOpenBook) {
            scene_manager_next_scene(app->scene_manager, BooksSceneReader);
            return true;
        }
        if(event.event == BooksEventConfirmDelete) {
            scene_manager_next_scene(app->scene_manager, BooksSceneConfirmDelete);
            return true;
        }
    }
    return false;
}

void books_scene_library_on_exit(void* ctx) {
    BooksApp* app = ctx;
    library_view_reset(app->library, "Library", false);
}
