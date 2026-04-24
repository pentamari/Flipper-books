#include "../books_app.h"
#include <stdio.h>

static char stats_text[160];

static void stats_dialog_cb(DialogExResult result, void* ctx) {
    BooksApp* app = ctx;
    if(result == DialogExResultLeft || result == DialogExResultBack) {
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventBackToLibrary);
    }
}

void books_scene_stats_on_enter(void* ctx) {
    BooksApp* app = ctx;
    uint32_t mins = app->stats.total_read_seconds / 60;
    uint32_t hours = mins / 60;
    snprintf(
        stats_text,
        sizeof(stats_text),
        "Time: %luh %lum\nPages: %lu\nOpened: %lu  Done: %lu",
        (unsigned long)hours,
        (unsigned long)(mins % 60),
        (unsigned long)app->stats.total_pages_read,
        (unsigned long)app->stats.books_opened,
        (unsigned long)app->stats.books_finished);

    DialogEx* d = app->dialog;
    dialog_ex_reset(d);
    dialog_ex_set_header(d, "Reading Stats", 64, 4, AlignCenter, AlignTop);
    dialog_ex_set_text(d, stats_text, 4, 18, AlignLeft, AlignTop);
    dialog_ex_set_left_button_text(d, "Back");
    dialog_ex_set_context(d, app);
    dialog_ex_set_result_callback(d, stats_dialog_cb);
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewDialog);
}

bool books_scene_stats_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    if(event.type == SceneManagerEventTypeCustom && event.event == BooksEventBackToLibrary) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void books_scene_stats_on_exit(void* ctx) {
    BooksApp* app = ctx;
    dialog_ex_reset(app->dialog);
}
