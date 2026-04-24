#include "../books_app.h"
#include <stdio.h>

enum {
    ItemResume,
    ItemBookmarks,
    ItemToc,
    ItemSearch,
    ItemGotoPercent,
    ItemFavorite,
    ItemMarkFinished,
    ItemSettings,
    ItemCloseBook,
};

static void submenu_cb(void* ctx, uint32_t index) {
    BooksApp* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void books_scene_menu_on_enter(void* ctx) {
    BooksApp* app = ctx;
    Submenu* m = app->submenu;
    submenu_reset(m);
    submenu_set_header(m, "Menu");
    submenu_add_item(m, "Resume", ItemResume, submenu_cb, app);
    submenu_add_item(m, "Bookmarks", ItemBookmarks, submenu_cb, app);
    submenu_add_item(m, "Contents", ItemToc, submenu_cb, app);
    submenu_add_item(m, "Search", ItemSearch, submenu_cb, app);
    submenu_add_item(m, "Go to %...", ItemGotoPercent, submenu_cb, app);

    /* Favourite toggle reflects current state in the label so users know what
     * pressing it will do. */
    char fav_label[24];
    snprintf(fav_label, sizeof(fav_label),
             app->progress.favorite ? "Unfavorite" : "Favorite");
    submenu_add_item(m, fav_label, ItemFavorite, submenu_cb, app);

    if(!app->progress.finished) {
        submenu_add_item(m, "Mark Finished", ItemMarkFinished, submenu_cb, app);
    }
    submenu_add_item(m, "Settings", ItemSettings, submenu_cb, app);
    submenu_add_item(m, "Close Book", ItemCloseBook, submenu_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewSubmenu);
}

bool books_scene_menu_on_event(void* ctx, SceneManagerEvent event) {
    BooksApp* app = ctx;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case ItemResume:
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case ItemBookmarks:
        scene_manager_next_scene(app->scene_manager, BooksSceneBookmarks);
        return true;
    case ItemToc:
        scene_manager_next_scene(app->scene_manager, BooksSceneTOC);
        return true;
    case ItemSearch:
        scene_manager_next_scene(app->scene_manager, BooksSceneSearchInput);
        return true;
    case ItemGotoPercent:
        scene_manager_next_scene(app->scene_manager, BooksSceneGotoPercent);
        return true;
    case ItemFavorite:
        app->progress.favorite = app->progress.favorite ? 0 : 1;
        book_progress_save(app->current_book_path, &app->progress);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case ItemMarkFinished:
        app->progress.finished = 1;
        app->stats.books_finished++;
        book_progress_save(app->current_book_path, &app->progress);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case ItemSettings:
        scene_manager_next_scene(app->scene_manager, BooksSceneSettings);
        return true;
    case ItemCloseBook:
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, BooksSceneStart);
        return true;
    }
    return false;
}

void books_scene_menu_on_exit(void* ctx) {
    BooksApp* app = ctx;
    submenu_reset(app->submenu);
}
