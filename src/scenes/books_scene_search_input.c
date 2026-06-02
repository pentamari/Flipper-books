#include "../books_app.h"

static void on_search_text(void* ctx) {
    BooksApp* app = ctx;
    // Search in currently open book (if any) by jumping via Reader handler
    FBook* b = fbook_alloc();
    if(b && fbook_open(b, app->current_book_path)) {
        uint32_t found = fbook_search(b, 0, app->text_input_buf);
        if(found != UINT32_MAX) {
            app->progress.offset = found;
            book_progress_save(app->current_book_path, &app->progress);
        }
    }
    fbook_free(b);
    view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventJumpTo);
}

void books_scene_search_input_on_enter(void* ctx) {
    BooksApp* app = ctx;
    TextInput* ti = app->text_input;
    text_input_reset(ti);
    text_input_set_header_text(ti, "Search text");
    text_input_set_result_callback(ti, on_search_text, app, app->text_input_buf, sizeof(app->text_input_buf), true);
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewTextInput);
}

bool books_scene_search_input_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type == SceneManagerEventTypeCustom && event.event == BooksEventJumpTo) {
        scene_manager_search_and_switch_to_another_scene(app->scene_manager, BooksSceneReader);
        return true;
    }
    return false;
}

void books_scene_search_input_on_exit(void* ctx) {
    BooksApp* app = ctx;
    text_input_reset(app->text_input);
}
