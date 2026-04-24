#include "book_progress.h"
#include "../books_app.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>
#include <stdio.h>

#define PROGRESS_MAGIC 0x50524731u /* 'PRG1' */

void book_progress_set_defaults(BookProgress* p) {
    memset(p, 0, sizeof(*p));
}

static void progress_path_for(const char* book_path, char* out, size_t out_len) {
    const char* slash = strrchr(book_path, '/');
    const char* name = slash ? slash + 1 : book_path;
    snprintf(out, out_len, "%s/%s.prg", BOOKS_PROGRESS, name);
}

typedef struct {
    uint32_t magic;
    uint32_t version;
    BookProgress data;
} ProgressBlob;

bool book_progress_load(const char* book_path, BookProgress* p) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    char path[256];
    progress_path_for(book_path, path, sizeof(path));
    File* f = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        ProgressBlob b;
        if(storage_file_read(f, &b, sizeof(b)) == sizeof(b) && b.magic == PROGRESS_MAGIC) {
            *p = b.data;
            ok = true;
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    if(!ok) book_progress_set_defaults(p);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool book_progress_save(const char* book_path, const BookProgress* p) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, BOOKS_PROGRESS);
    char path[256];
    progress_path_for(book_path, path, sizeof(path));
    File* f = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ProgressBlob b = {.magic = PROGRESS_MAGIC, .version = 1, .data = *p};
        ok = storage_file_write(f, &b, sizeof(b)) == sizeof(b);
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool book_progress_add_bookmark(BookProgress* p, uint32_t offset, uint32_t page, const char* label) {
    if(p->bookmark_count >= BOOKS_MAX_BOOKMARKS) return false;
    Bookmark* b = &p->bookmarks[p->bookmark_count++];
    b->offset = offset;
    b->page = page;
    b->timestamp = furi_get_tick();
    strncpy(b->label, label ? label : "Mark", sizeof(b->label) - 1);
    b->label[sizeof(b->label) - 1] = '\0';
    return true;
}

bool book_progress_remove_bookmark(BookProgress* p, uint16_t index) {
    if(index >= p->bookmark_count) return false;
    for(uint16_t i = index; i < (uint16_t)(p->bookmark_count - 1); ++i) {
        p->bookmarks[i] = p->bookmarks[i + 1];
    }
    p->bookmark_count--;
    return true;
}
