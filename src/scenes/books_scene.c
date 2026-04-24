#include "books_scene.h"

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "books_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) bool prefix##_scene_##name##_on_event(void*, SceneManagerEvent);
#include "books_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void*);
#include "books_scene_config.h"
#undef ADD_SCENE

static void (*const books_scene_on_enter_handlers[])(void*) = {
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
#include "books_scene_config.h"
#undef ADD_SCENE
};

static bool (*const books_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
#include "books_scene_config.h"
#undef ADD_SCENE
};

static void (*const books_scene_on_exit_handlers[])(void*) = {
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
#include "books_scene_config.h"
#undef ADD_SCENE
};

const SceneManagerHandlers books_scene_handlers = {
    .on_enter_handlers = books_scene_on_enter_handlers,
    .on_event_handlers = books_scene_on_event_handlers,
    .on_exit_handlers = books_scene_on_exit_handlers,
    .scene_num = BooksSceneCount,
};
