#pragma once

#include <gui/scene_manager.h>

typedef enum {
    BooksSceneStart,
    BooksSceneLibrary,
    BooksSceneReader,
    BooksSceneMenu,
    BooksSceneSettings,
    BooksSceneBookmarks,
    BooksSceneAddBookmark,
    BooksSceneTOC,
    BooksSceneSearch,
    BooksSceneSearchInput,
    BooksSceneStats,
    BooksSceneAbout,
    BooksSceneImport,
    BooksSceneConfirmDelete,
    BooksSceneCount,
} BooksScene;

extern const SceneManagerHandlers books_scene_handlers;

typedef enum {
    BooksEventOpenBook = 0x1000,
    BooksEventOpenMenu,
    BooksEventResume,
    BooksEventToggleBookmark,
    BooksEventShowTOC,
    BooksEventShowSearch,
    BooksEventShowSettings,
    BooksEventShowStats,
    BooksEventCloseBook,
    BooksEventJumpTo,
    BooksEventBookmarkChosen,
    BooksEventSearchStart,
    BooksEventSearchNext,
    BooksEventImportScan,
    BooksEventBackToLibrary,
    BooksEventExit,
} BooksCustomEvent;
