#include "../books_app.h"
#include <stdio.h>
#include <string.h>

static char prompt_buf[96];

static void dialog_cb(DialogExResult result, void* ctx) {
    BooksApp* app = ctx;
    if(result == DialogExResultRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventDoDelete);
    } else {
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventCancelDelete);
    }
}

void books_scene_confirm_delete_on_enter(void* ctx) {
    BooksApp* app = ctx;
    DialogEx* d = app->dialog;
    dialog_ex_reset(d);

    /* Truncate long file names so they fit on the tiny screen. */
    const char* name = app->pending_delete_name;
    if(!name[0]) name = "this book";
    snprintf(prompt_buf, sizeof(prompt_buf), "%.40s\nand its progress?", name);

    dialog_ex_set_header(d, "Delete?", 64, 4, AlignCenter, AlignTop);
    dialog_ex_set_text(d, prompt_buf, 4, 20, AlignLeft, AlignTop);
    dialog_ex_set_left_button_text(d, "Cancel");
    dialog_ex_set_right_button_text(d, "Delete");
    dialog_ex_set_context(d, app);
    dialog_ex_set_result_callback(d, dialog_cb);

    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewDialog);
}

bool books_scene_confirm_delete_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BooksEventDoDelete) {
            fbook_delete(app->pending_delete_path);
            /* Clear current_book_path if we just removed the book the user was on. */
            if(strcmp(app->current_book_path, app->pending_delete_path) == 0) {
                app->current_book_path[0] = '\0';
                app->current_book_name[0] = '\0';
                app->progress.offset = 0;
            }
            app->pending_delete_path[0] = '\0';
            app->pending_delete_name[0] = '\0';
            /* Returns to the library, which rescans on enter and drops the entry. */
            scene_manager_previous_scene(app->scene_manager);
            return true;
        }
        if(event.event == BooksEventCancelDelete) {
            scene_manager_previous_scene(app->scene_manager);
            return true;
        }
    }
    return false;
}

void books_scene_confirm_delete_on_exit(void* ctx) {
    BooksApp* app = ctx;
    dialog_ex_reset(app->dialog);
}
