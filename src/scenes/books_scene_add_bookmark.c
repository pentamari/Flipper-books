#include "../books_app.h"

static void text_input_cb(void* ctx) {
    BooksApp* app = ctx;
    book_progress_load(app->current_book_path, &app->progress);
    book_progress_add_bookmark(&app->progress, app->progress.offset, app->progress.page, app->text_input_buf);
    book_progress_save(app->current_book_path, &app->progress);
    scene_manager_previous_scene(app->scene_manager);
}

void books_scene_add_bookmark_on_enter(void* ctx) {
    BooksApp* app = ctx;
    TextInput* ti = app->text_input;
    text_input_reset(ti);
    text_input_set_header_text(ti, "Bookmark label");
    app->text_input_buf[0] = '\0';
    text_input_set_result_callback(ti, text_input_cb, app, app->text_input_buf, sizeof(app->text_input_buf), true);
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewTextInput);
}

bool books_scene_add_bookmark_on_event(void* ctx, SceneManagerEvent event) {
    UNUSED(ctx);
    UNUSED(event);
    return false;
}

void books_scene_add_bookmark_on_exit(void* ctx) {
    BooksApp* app = ctx;
    text_input_reset(app->text_input);
}
