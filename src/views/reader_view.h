#pragma once

#include <gui/view.h>
#include "../helpers/book_storage.h"
#include "../helpers/book_settings.h"
#include "../helpers/book_progress.h"

typedef struct ReaderView ReaderView;

typedef enum {
    ReaderEventBack = 0,
    ReaderEventMenu,
    ReaderEventBookmark,
    ReaderEventToc,
    ReaderEventSearch,
    ReaderEventSearchNext,
} ReaderPublicEvent;

typedef void (*ReaderEventCallback)(ReaderPublicEvent event, void* context);

ReaderView* reader_view_alloc(void);
void reader_view_free(ReaderView* r);
View* reader_view_get_view(ReaderView* r);

void reader_view_set_book(ReaderView* r, FBook* book);
void reader_view_set_settings(ReaderView* r, const BookSettings* settings);
void reader_view_set_progress(ReaderView* r, const BookProgress* progress);

/** Current reading offset (for saving progress on exit). */
uint32_t reader_view_get_offset(const ReaderView* r);
uint32_t reader_view_get_page(const ReaderView* r);

void reader_view_jump_to(ReaderView* r, uint32_t offset);

void reader_view_set_event_callback(ReaderView* r, ReaderEventCallback cb, void* ctx);
