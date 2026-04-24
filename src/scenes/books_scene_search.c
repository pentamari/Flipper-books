#include "../books_app.h"

void books_scene_search_on_enter(void* ctx) {
    BooksApp* app = ctx;
    scene_manager_next_scene(app->scene_manager, BooksSceneSearchInput);
}

bool books_scene_search_on_event(void* ctx, SceneManagerEvent event) {
    UNUSED(ctx);
    UNUSED(event);
    return false;
}

void books_scene_search_on_exit(void* ctx) {
    UNUSED(ctx);
}
