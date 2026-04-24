#include "book_storage.h"
#include "../books_app.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define FBOOK_CHAPTER_SIZE 36
#define FBOOK_IMAGE_SIZE   16

FBook* fbook_alloc(void) {
    FBook* b = malloc(sizeof(FBook));
    memset(b, 0, sizeof(*b));
    return b;
}

void fbook_free(FBook* b) {
    if(!b) return;
    fbook_close(b);
    free(b);
}

void fbook_close(FBook* b) {
    if(b->file) {
        storage_file_close(b->file);
        storage_file_free(b->file);
        b->file = NULL;
    }
    if(b->storage) {
        furi_record_close(RECORD_STORAGE);
        b->storage = NULL;
    }
}

static uint16_t read_u16(File* f) {
    uint8_t buf[2];
    storage_file_read(f, buf, 2);
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t read_u32(File* f) {
    uint8_t buf[4];
    storage_file_read(f, buf, 4);
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static bool parse_header(FBook* b) {
    char magic[FBOOK_MAGIC_LEN];
    if(storage_file_read(b->file, magic, FBOOK_MAGIC_LEN) != FBOOK_MAGIC_LEN) return false;
    if(memcmp(magic, FBOOK_MAGIC, FBOOK_MAGIC_LEN) != 0) return false;
    uint16_t version = read_u16(b->file);
    if(version != 1) return false;

    if(storage_file_read(b->file, b->title, FBOOK_MAX_TITLE) != FBOOK_MAX_TITLE) return false;
    if(storage_file_read(b->file, b->author, FBOOK_MAX_AUTHOR) != FBOOK_MAX_AUTHOR) return false;
    b->title[FBOOK_MAX_TITLE - 1] = '\0';
    b->author[FBOOK_MAX_AUTHOR - 1] = '\0';

    b->text_offset = read_u32(b->file);
    b->text_length = read_u32(b->file);
    b->chapter_count = read_u16(b->file);
    b->image_count = read_u16(b->file);

    if(b->chapter_count > FBOOK_MAX_CHAPTERS) b->chapter_count = FBOOK_MAX_CHAPTERS;
    if(b->image_count > FBOOK_MAX_IMAGES) b->image_count = FBOOK_MAX_IMAGES;

    for(uint16_t i = 0; i < b->chapter_count; ++i) {
        b->chapters[i].offset = read_u32(b->file);
        if(storage_file_read(b->file, b->chapters[i].title, 32) != 32) return false;
        b->chapters[i].title[31] = '\0';
    }
    for(uint16_t i = 0; i < b->image_count; ++i) {
        b->images[i].offset_in_text = read_u32(b->file);
        b->images[i].w = read_u16(b->file);
        b->images[i].h = read_u16(b->file);
        b->images[i].data_offset = read_u32(b->file);
        b->images[i].data_len = 0;
    }
    /* image data_len field comes from a trailing pass to keep layout simple:
       we store data packed after the last image entry and use data_offset to
       point absolutely into the file; data_len is implicit = w*h/8 rounded up. */
    for(uint16_t i = 0; i < b->image_count; ++i) {
        b->images[i].data_len = ((uint32_t)b->images[i].w * b->images[i].h + 7) / 8;
    }
    return true;
}

bool fbook_open(FBook* b, const char* path) {
    fbook_close(b);
    b->storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(b->storage, BOOKS_APP_FOLDER);
    storage_simply_mkdir(b->storage, BOOKS_LIBRARY);
    storage_simply_mkdir(b->storage, BOOKS_CACHE);
    storage_simply_mkdir(b->storage, BOOKS_PROGRESS);
    storage_simply_mkdir(b->storage, BOOKS_BOOKMARKS);

    char resolved[256];
    strncpy(resolved, path, sizeof(resolved) - 1);
    resolved[sizeof(resolved) - 1] = '\0';

    const char* dot = strrchr(path, '.');
    if(dot) {
        if(strcasecmp(dot, BOOKS_EXT_EPUB) == 0) {
            if(!fbook_import_epub(path, resolved, sizeof(resolved))) {
                fbook_close(b);
                return false;
            }
        } else if(strcasecmp(dot, BOOKS_EXT_TXT) == 0) {
            if(!fbook_import_txt(path, resolved, sizeof(resolved))) {
                fbook_close(b);
                return false;
            }
        }
    }

    strncpy(b->path, resolved, sizeof(b->path) - 1);
    b->file = storage_file_alloc(b->storage);
    if(!storage_file_open(b->file, resolved, FSAM_READ, FSOM_OPEN_EXISTING)) {
        fbook_close(b);
        return false;
    }
    if(!parse_header(b)) {
        fbook_close(b);
        return false;
    }
    return true;
}

uint32_t fbook_read(FBook* b, uint32_t offset, char* out, uint32_t max_bytes) {
    if(!b->file || offset >= b->text_length) return 0;
    if(offset + max_bytes > b->text_length) {
        max_bytes = b->text_length - offset;
    }
    if(!storage_file_seek(b->file, b->text_offset + offset, true)) return 0;
    return storage_file_read(b->file, out, max_bytes);
}

uint8_t* fbook_load_image(FBook* b, uint16_t index, uint16_t* w, uint16_t* h) {
    if(index >= b->image_count) return NULL;
    FBookImage* img = &b->images[index];
    uint8_t* buf = malloc(img->data_len);
    if(!buf) return NULL;
    if(!storage_file_seek(b->file, img->data_offset, true)) {
        free(buf);
        return NULL;
    }
    if(storage_file_read(b->file, buf, img->data_len) != img->data_len) {
        free(buf);
        return NULL;
    }
    *w = img->w;
    *h = img->h;
    return buf;
}

uint16_t fbook_find_chapter(const FBook* b, uint32_t offset) {
    uint16_t idx = 0;
    for(uint16_t i = 0; i < b->chapter_count; ++i) {
        if(b->chapters[i].offset <= offset) idx = i;
        else break;
    }
    return idx;
}

uint16_t fbook_next_image(const FBook* b, uint32_t offset) {
    for(uint16_t i = 0; i < b->image_count; ++i) {
        if(b->images[i].offset_in_text >= offset) return i;
    }
    return UINT16_MAX;
}

uint32_t fbook_search(FBook* b, uint32_t start_offset, const char* needle) {
    if(!needle || !*needle) return UINT32_MAX;
    size_t nlen = strlen(needle);
    if(nlen > 31) nlen = 31;
    char window[128];
    uint32_t pos = start_offset;
    while(pos < b->text_length) {
        uint32_t got = fbook_read(b, pos, window, sizeof(window) - 1);
        if(!got) break;
        window[got] = '\0';
        for(uint32_t i = 0; i + nlen <= got; ++i) {
            bool ok = true;
            for(size_t j = 0; j < nlen; ++j) {
                char a = window[i + j];
                char c = needle[j];
                if(a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
                if(c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
                if(a != c) {
                    ok = false;
                    break;
                }
            }
            if(ok) return pos + i;
        }
        if(got <= nlen) break;
        pos += got - nlen; // overlap window
    }
    return UINT32_MAX;
}

static void write_u16(File* f, uint16_t v) {
    uint8_t buf[2] = {v & 0xFF, (v >> 8) & 0xFF};
    storage_file_write(f, buf, 2);
}

static void write_u32(File* f, uint32_t v) {
    uint8_t buf[4] = {v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF};
    storage_file_write(f, buf, 4);
}

static void write_fixed(File* f, const char* s, size_t n) {
    uint8_t buf[128];
    if(n > sizeof(buf)) n = sizeof(buf);
    size_t l = strlen(s);
    if(l > n) l = n;
    memset(buf, 0, n);
    memcpy(buf, s, l);
    storage_file_write(f, buf, n);
}

static bool write_fbook_header(
    File* out,
    const char* title,
    const char* author,
    uint32_t text_length,
    uint32_t text_offset) {
    if(storage_file_write(out, FBOOK_MAGIC, FBOOK_MAGIC_LEN) != FBOOK_MAGIC_LEN) return false;
    write_u16(out, 1);
    write_fixed(out, title, FBOOK_MAX_TITLE);
    write_fixed(out, author, FBOOK_MAX_AUTHOR);
    write_u32(out, text_offset);
    write_u32(out, text_length);
    write_u16(out, 0); // chapters
    write_u16(out, 0); // images
    return true;
}

static const char* basename_of(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void cache_path_for(const char* src, char* out, size_t out_len) {
    char base[128];
    strncpy(base, basename_of(src), sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    char* dot = strrchr(base, '.');
    if(dot) *dot = '\0';
    snprintf(out, out_len, "%s/%s.fbook", BOOKS_CACHE, base);
}

bool fbook_import_txt(const char* txt_path, char* out_path, size_t out_len) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, BOOKS_CACHE);
    char dst[256];
    cache_path_for(txt_path, dst, sizeof(dst));

    File* in = storage_file_alloc(storage);
    File* out = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(in, txt_path, FSAM_READ, FSOM_OPEN_EXISTING) &&
       storage_file_open(out, dst, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        uint32_t total = storage_file_size(in);
        uint32_t header_size = FBOOK_MAGIC_LEN + 2 + FBOOK_MAX_TITLE + FBOOK_MAX_AUTHOR + 4 + 4 + 2 + 2;
        /* storage_file_size can leave the read cursor anywhere; anchor it. */
        storage_file_seek(in, 0, true);
        bool header_ok = write_fbook_header(out, basename_of(txt_path), "Unknown", total, header_size);

        uint8_t buf[256];
        uint32_t remaining = total;
        ok = header_ok;
        while(ok && remaining > 0) {
            uint32_t want = remaining > sizeof(buf) ? sizeof(buf) : remaining;
            uint16_t got = storage_file_read(in, buf, (uint16_t)want);
            if(!got) {
                ok = false;
                break;
            }
            if(storage_file_write(out, buf, got) != got) {
                ok = false;
                break;
            }
            remaining -= got;
        }
    }
    storage_file_close(in);
    storage_file_close(out);
    storage_file_free(in);
    storage_file_free(out);
    furi_record_close(RECORD_STORAGE);

    if(ok) {
        strncpy(out_path, dst, out_len - 1);
        out_path[out_len - 1] = '\0';
    }
    return ok;
}

/* EPUB is a ZIP with UTF-8 XHTML files. Parsing deflate on-device would be
 * heavyweight, so we look for an already-converted .fbook sidecar in the cache
 * and fail gracefully otherwise. The Python tool tools/epub_to_fbook.py is the
 * recommended conversion path (documented in README). */
bool fbook_import_epub(const char* epub_path, char* out_path, size_t out_len) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, BOOKS_CACHE);
    char dst[256];
    cache_path_for(epub_path, dst, sizeof(dst));
    bool ok = storage_common_exists(storage, dst) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    if(ok) {
        strncpy(out_path, dst, out_len - 1);
        out_path[out_len - 1] = '\0';
    }
    return ok;
}

bool fbook_delete(const char* book_path) {
    if(!book_path || !*book_path) return false;
    Storage* storage = furi_record_open(RECORD_STORAGE);

    bool primary_ok = storage_simply_remove(storage, book_path);

    /* Remove the cached .fbook derived from this file's basename. */
    char cached[256];
    cache_path_for(book_path, cached, sizeof(cached));
    if(strcmp(cached, book_path) != 0) {
        storage_simply_remove(storage, cached);
    }

    /* Remove the progress sidecar for this book. */
    const char* slash = strrchr(book_path, '/');
    const char* name = slash ? slash + 1 : book_path;
    char prg[256];
    snprintf(prg, sizeof(prg), "%s/%s.prg", BOOKS_PROGRESS, name);
    storage_simply_remove(storage, prg);

    furi_record_close(RECORD_STORAGE);
    return primary_ok;
}

uint16_t fbook_scan_library(char paths[][256], uint16_t max_paths) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, BOOKS_LIBRARY);
    storage_simply_mkdir(storage, BOOKS_CACHE);

    uint16_t count = 0;
    const char* dirs[] = {BOOKS_LIBRARY, BOOKS_CACHE};
    for(size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]) && count < max_paths; ++d) {
        File* dir = storage_file_alloc(storage);
        if(storage_dir_open(dir, dirs[d])) {
            FileInfo info;
            char name[128];
            while(count < max_paths &&
                  storage_dir_read(dir, &info, name, sizeof(name))) {
                if(file_info_is_dir(&info)) continue;
                const char* dot = strrchr(name, '.');
                if(!dot) continue;
                if(strcasecmp(dot, BOOKS_EXT_EPUB) != 0 &&
                   strcasecmp(dot, BOOKS_EXT_FBOOK) != 0 &&
                   strcasecmp(dot, BOOKS_EXT_TXT) != 0) continue;
                snprintf(paths[count], 256, "%s/%s", dirs[d], name);
                count++;
            }
            storage_dir_close(dir);
        }
        storage_file_free(dir);
    }
    furi_record_close(RECORD_STORAGE);
    return count;
}
