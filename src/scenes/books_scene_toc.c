#include "../books_app.h"
#include <stdio.h>

static FBook* cached_book;
/* Submenu only stores a pointer to the label string, so we keep our own
 * static buffers alive for the whole scene. */
static char toc_labels[FBOOK_MAX_CHAPTERS][80];

static void submenu_cb(void* ctx, uint32_t index) {
    BooksApp* app = ctx;
    if(!cached_book || index >= cached_book->chapter_count) return;
    app->progress.offset = cached_book->chapters[index].offset;
    app->progress.page = 0;
    book_progress_save(app->current_book_path, &app->progress);
    view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventJumpTo);
}

void books_scene_toc_on_enter(void* ctx) {
    BooksApp* app = ctx;
    Submenu* m = app->submenu;
    submenu_reset(m);
    submenu_set_header(m, "Contents");
    cached_book = fbook_alloc();
    if(!fbook_open(cached_book, app->current_book_path)) {
        submenu_add_item(m, "(no TOC)", 0xFFFF, NULL, app);
        fbook_free(cached_book);
        cached_book = NULL;
    } else if(cached_book->chapter_count == 0) {
        submenu_add_item(m, "(no chapters)", 0xFFFF, NULL, app);
    } else {
        for(uint16_t i = 0; i < cached_book->chapter_count; ++i) {
            const char* title = cached_book->chapters[i].title;
            uint32_t wc = cached_book->chapters[i].word_count;
            if(wc > 0) {
                snprintf(toc_labels[i], sizeof(toc_labels[i]),
                         "%s (%lu w)", title, (unsigned long)wc);
            } else {
                snprintf(toc_labels[i], sizeof(toc_labels[i]), "%s", title);
            }
            submenu_add_item(m, toc_labels[i], i, submenu_cb, app);
        }
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewSubmenu);
}

bool books_scene_toc_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type == SceneManagerEventTypeCustom && event.event == BooksEventJumpTo) {
        scene_manager_search_and_switch_to_another_scene(app->scene_manager, BooksSceneReader);
        return true;
    }
    return false;
}

void books_scene_toc_on_exit(void* ctx) {
    BooksApp* app = ctx;
    submenu_reset(app->submenu);
    if(cached_book) {
        fbook_free(cached_book);
        cached_book = NULL;
    }
}
