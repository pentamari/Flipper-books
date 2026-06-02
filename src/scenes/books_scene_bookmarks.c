#include "../books_app.h"
#include <stdio.h>

static void submenu_cb(void* ctx, uint32_t index) {
    BooksApp* app = ctx;
    if(index < app->progress.bookmark_count) {
        app->progress.offset = app->progress.bookmarks[index].offset;
        app->progress.page = app->progress.bookmarks[index].page;
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventBookmarkChosen);
    }
}

void books_scene_bookmarks_on_enter(void* ctx) {
    BooksApp* app = ctx;
    Submenu* m = app->submenu;
    submenu_reset(m);
    submenu_set_header(m, "Bookmarks");
    book_progress_load(app->current_book_path, &app->progress);
    if(app->progress.bookmark_count == 0) {
        submenu_add_item(m, "(none yet)", 0xFFFF, NULL, app);
    } else {
        char label[40];
        for(uint16_t i = 0; i < app->progress.bookmark_count; ++i) {
            snprintf(label, sizeof(label), "%u%%  %s",
                     (unsigned)(app->progress.total_bytes ?
                        ((uint64_t)app->progress.bookmarks[i].offset * 100u /
                         app->progress.total_bytes) : 0),
                     app->progress.bookmarks[i].label);
            submenu_add_item(m, label, i, submenu_cb, app);
        }
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewSubmenu);
}

bool books_scene_bookmarks_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type == SceneManagerEventTypeCustom && event.event == BooksEventBookmarkChosen) {
        book_progress_save(app->current_book_path, &app->progress);
        scene_manager_search_and_switch_to_another_scene(app->scene_manager, BooksSceneReader);
        return true;
    }
    return false;
}

void books_scene_bookmarks_on_exit(void* ctx) {
    BooksApp* app = ctx;
    submenu_reset(app->submenu);
}
