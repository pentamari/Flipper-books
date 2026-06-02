#include "../books_app.h"
#include <stdio.h>
#include <string.h>

/* Lightweight 0..100 picker that uses the existing VariableItemList view so
 * we don't have to introduce another module. Press OK on the row to confirm. */

static void value_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    uint8_t pct = (uint8_t)variable_item_get_current_value_index(item);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", pct);
    variable_item_set_current_value_text(item, buf);
    /* Stash on the app so the OK handler in on_event can pick it up. */
    app->goto_percent_pending = pct;
}

static void enter_cb(void* ctx, uint32_t index) {
    BooksApp* app = ctx;
    UNUSED(index);
    view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventJumpPercent);
}

void books_scene_goto_percent_on_enter(void* ctx) {
    BooksApp* app = ctx;
    VariableItemList* l = app->var_list;
    variable_item_list_reset(l);

    uint8_t initial = 0;
    if(app->progress.total_bytes > 0) {
        uint32_t p =
            (uint32_t)((uint64_t)app->progress.offset * 100u / app->progress.total_bytes);
        if(p > 100) p = 100;
        initial = (uint8_t)p;
    }
    app->goto_percent_pending = initial;

    VariableItem* it = variable_item_list_add(l, "Jump to", 101, value_cb, app);
    variable_item_set_current_value_index(it, initial);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", initial);
    variable_item_set_current_value_text(it, buf);

    variable_item_list_set_enter_callback(l, enter_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewVarList);
}

bool books_scene_goto_percent_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type == SceneManagerEventTypeCustom &&
       event.event == BooksEventJumpPercent) {
        if(app->progress.total_bytes > 0) {
            app->progress.offset =
                (uint32_t)((uint64_t)app->goto_percent_pending *
                           app->progress.total_bytes / 100u);
        }
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, BooksSceneReader);
        view_dispatcher_send_custom_event(app->view_dispatcher, BooksEventJumpTo);
        return true;
    }
    return false;
}

void books_scene_goto_percent_on_exit(void* ctx) {
    BooksApp* app = ctx;
    variable_item_list_reset(app->var_list);
}
