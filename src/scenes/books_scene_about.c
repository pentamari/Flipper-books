#include "../books_app.h"

void books_scene_about_on_enter(void* ctx) {
    BooksApp* app = ctx;
    DialogEx* d = app->dialog;
    dialog_ex_reset(d);
    dialog_ex_set_header(d, "Books v1.0", 64, 4, AlignCenter, AlignTop);
    dialog_ex_set_text(d,
        "EPUB reader for Flipper.\n"
        "Put EPUBs into\n"
        "apps_data/books/library",
        4, 18, AlignLeft, AlignTop);
    dialog_ex_set_left_button_text(d, "Back");
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewDialog);
}

bool books_scene_about_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type == SceneManagerEventTypeBack ||
       (event.type == SceneManagerEventTypeCustom && event.event == DialogExResultLeft)) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void books_scene_about_on_exit(void* ctx) {
    BooksApp* app = ctx;
    dialog_ex_reset(app->dialog);
}
