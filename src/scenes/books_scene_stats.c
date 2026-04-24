#include "../books_app.h"
#include <stdio.h>

static char stats_text[160];

static void stats_dialog_cb(DialogExResult result, void* ctx) {
    BooksApp* app = ctx;
    if(result == DialogExResultLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventBackToLibrary);
    }
}

void books_scene_stats_on_enter(void* ctx) {
    BooksApp* app = ctx;
    uint32_t mins = app->stats.total_read_seconds / 60;
    uint32_t hours = mins / 60;

    /* Reading speed in words/minute. Falls back to ~250 (a typical adult
     * reading rate) until we have at least a minute of reading time on
     * record, otherwise the first sample swings wildly. */
    uint32_t wpm = 250;
    if(app->stats.total_read_seconds >= 60 && app->stats.total_words_read > 0) {
        uint64_t denom = app->stats.total_read_seconds;
        wpm = (uint32_t)((uint64_t)app->stats.total_words_read * 60u / denom);
        if(wpm < 30) wpm = 30;
        if(wpm > 1500) wpm = 1500;
    }

    /* Time-remaining in the *current* book, computed from per-book progress.
     * Skipped if the user is not currently in a book. */
    char eta[40] = "";
    if(app->progress.words_in_book > 0 && app->progress.total_bytes > 0) {
        uint32_t remaining_words =
            app->progress.words_in_book -
            (uint32_t)((uint64_t)app->progress.words_in_book *
                       app->progress.offset / app->progress.total_bytes);
        uint32_t eta_min = remaining_words / wpm;
        if(eta_min > 0) {
            snprintf(eta, sizeof(eta), "\nETA: %luh %lum",
                     (unsigned long)(eta_min / 60),
                     (unsigned long)(eta_min % 60));
        }
    }

    snprintf(
        stats_text,
        sizeof(stats_text),
        "Time: %luh %lum\nPages: %lu  WPM: %lu\nOpened: %lu  Done: %lu%s",
        (unsigned long)hours,
        (unsigned long)(mins % 60),
        (unsigned long)app->stats.total_pages_read,
        (unsigned long)wpm,
        (unsigned long)app->stats.books_opened,
        (unsigned long)app->stats.books_finished,
        eta);

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
