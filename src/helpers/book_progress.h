#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BOOKS_MAX_BOOKMARKS 8

typedef struct {
    uint32_t offset;         // byte offset into book text
    uint32_t page;           // last rendered page
    uint32_t timestamp;      // unix-ish tick
    char label[32];
} Bookmark;

typedef struct {
    uint32_t offset;         // last reading byte offset
    uint32_t page;           // last rendered page number
    uint32_t total_bytes;    // content size
    uint32_t last_read;      // tick / unix-ish; sortable
    uint16_t last_chapter;
    uint16_t bookmark_count;
    Bookmark bookmarks[BOOKS_MAX_BOOKMARKS];

    // v2 additions
    uint32_t total_seconds_read;  // accumulated reading time on this book
    uint32_t pages_turned;
    uint32_t words_in_book;       // copied from FBook word_count
    uint8_t  favorite;            // 0 = no, 1 = yes
    uint8_t  finished;            // 1 once user reached the end
    uint8_t  reserved[14];
} BookProgress;

void book_progress_set_defaults(BookProgress* p);

/** Load progress for a given book path, resolving to a sidecar file. */
bool book_progress_load(const char* book_path, BookProgress* p);
bool book_progress_save(const char* book_path, const BookProgress* p);

bool book_progress_add_bookmark(BookProgress* p, uint32_t offset, uint32_t page, const char* label);
bool book_progress_remove_bookmark(BookProgress* p, uint16_t index);

/** Read just the lightweight summary without loading bookmarks. */
bool book_progress_load_summary(const char* book_path,
                                uint32_t* offset,
                                uint32_t* total_bytes,
                                uint32_t* last_read,
                                uint8_t* favorite);
