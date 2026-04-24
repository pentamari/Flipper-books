#include "../books_app.h"
#include <string.h>
#include <strings.h>

static FBook* g_book = NULL;

static void open_fail_popup_cb(void* ctx) {
    BooksApp* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventCloseBook);
}

static void reader_event_cb(ReaderPublicEvent ev, void* ctx) {
    BooksApp* app = ctx;
    switch(ev) {
    case ReaderEventBack:
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventCloseBook);
        break;
    case ReaderEventMenu:
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventOpenMenu);
        break;
    case ReaderEventBookmark:
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventToggleBookmark);
        break;
    case ReaderEventToc:
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventShowTOC);
        break;
    case ReaderEventSearch:
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventShowSearch);
        break;
    case ReaderEventSearchNext:
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventSearchNext);
        break;
    }
}

void books_scene_reader_on_enter(void* ctx) {
    BooksApp* app = ctx;

    if(!g_book) {
        g_book = fbook_alloc();
    }
    if(!fbook_open(g_book, app->current_book_path)) {
        // failed to open - show popup that returns to the library after a tap or timeout
        popup_reset(app->popup);
        const char* dot = strrchr(app->current_book_path, '.');
        bool is_epub = dot && strcasecmp(dot, BOOKS_EXT_EPUB) == 0;
        popup_set_header(app->popup, "Cannot open", 64, 8, AlignCenter, AlignTop);
        popup_set_text(
            app->popup,
            is_epub ? "Run epub_to_fbook.py\nand drop the .fbook in"
                    : "File is missing or\nnot a valid book",
            64,
            28,
            AlignCenter,
            AlignTop);
        popup_set_context(app->popup, app);
        popup_set_callback(app->popup, open_fail_popup_cb);
        popup_set_timeout(app->popup, 2500);
        popup_enable_timeout(app->popup);
        view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewPopup);
        fbook_free(g_book);
        g_book = NULL;
        return;
    }

    book_progress_load(app->current_book_path, &app->progress);
    app->progress.total_bytes = g_book->text_length;

    reader_view_set_settings(app->reader, &app->settings);
    reader_view_set_book(app->reader, g_book);
    reader_view_set_progress(app->reader, &app->progress);
    reader_view_set_event_callback(app->reader, reader_event_cb, app);

    app->stats.books_opened++;
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewReader);
}

static void save_progress(BooksApp* app) {
    if(!g_book) return;
    app->progress.offset = reader_view_get_offset(app->reader);
    app->progress.page = reader_view_get_page(app->reader);
    app->progress.total_bytes = g_book->text_length;
    app->progress.last_read = furi_get_tick();
    app->progress.last_chapter = fbook_find_chapter(g_book, app->progress.offset);
    book_progress_save(app->current_book_path, &app->progress);
    app->stats.total_pages_read++;
}

bool books_scene_reader_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case BooksEventCloseBook:
        save_progress(app);
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, BooksSceneStart);
        return true;
    case BooksEventOpenMenu:
        save_progress(app);
        scene_manager_next_scene(app->scene_manager, BooksSceneMenu);
        return true;
    case BooksEventToggleBookmark:
        save_progress(app);
        scene_manager_next_scene(app->scene_manager, BooksSceneAddBookmark);
        return true;
    case BooksEventShowTOC:
        save_progress(app);
        scene_manager_next_scene(app->scene_manager, BooksSceneTOC);
        return true;
    case BooksEventSearchNext: {
        if(!g_book) return true;
        uint32_t off = reader_view_get_offset(app->reader);
        uint32_t found = fbook_search(g_book, off + 1, app->text_input_buf);
        if(found != UINT32_MAX) reader_view_jump_to(app->reader, found);
        return true;
    }
    case BooksEventJumpTo: {
        reader_view_jump_to(app->reader, app->progress.offset);
        return true;
    }
    }
    return false;
}

void books_scene_reader_on_exit(void* ctx) {
    BooksApp* app = ctx;
    if(g_book) {
        save_progress(app);
        fbook_free(g_book);
        g_book = NULL;
    }
}
