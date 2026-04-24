#pragma once

#include <gui/view.h>
#include <stdint.h>
#include <stdbool.h>

#include "../helpers/book_settings.h"

/* Custom library view: displays each book with a cover thumbnail, title and a
 * small progress bar inline. Replaces the generic submenu used previously so
 * users can see at a glance what they have, what they've read, and which book
 * is the cover art for which file.
 *
 * Memory notes: we cap at LIBRARY_VIEW_MAX entries and store one 64x64 1bpp
 * thumbnail per entry, which is ~512 bytes/entry. The whole view weighs in
 * around 13 KB when full - comparable to the 16 KB the old submenu cost in
 * static path storage. */

#define LIBRARY_VIEW_MAX 24

typedef void (*LibraryViewSelectCb)(uint16_t index, void* ctx);

typedef struct LibraryView LibraryView;

LibraryView* library_view_alloc(void);
void library_view_free(LibraryView* v);
View* library_view_get_view(LibraryView* v);

/** Reset the entry list and start a fresh load. Call this in the scene
 *  on_enter before the per-row library_view_add_entry calls. */
void library_view_reset(LibraryView* v, const char* header, bool delete_mode);

/** Add a single book entry. Cover may be NULL (no cover available); the view
 *  takes ownership of cover_data and frees it on the next reset. */
void library_view_add_entry(
    LibraryView* v,
    const char* path,
    const char* title,
    uint8_t progress_pct,
    uint32_t last_read,
    bool favorite,
    uint8_t* cover_data,
    uint16_t cover_w,
    uint16_t cover_h);

/** Sort the populated entry list in place. Stable across calls. */
void library_view_apply_sort(LibraryView* v, LibrarySort sort);

/** Read the path of the currently-highlighted row. Returns NULL when empty. */
const char* library_view_selected_path(LibraryView* v);

void library_view_set_select_callback(LibraryView* v, LibraryViewSelectCb cb, void* ctx);
