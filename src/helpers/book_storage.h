#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <storage/storage.h>

#define FBOOK_MAGIC      "FBOOK\x01"
#define FBOOK_MAGIC_LEN  6
#define FBOOK_MAX_TITLE  64
#define FBOOK_MAX_AUTHOR 48
#define FBOOK_MAX_CHAPTERS 128
#define FBOOK_MAX_IMAGES 64

/*
 * .fbook on-disk format (little endian):
 *   char     magic[6]        = "FBOOK\x01"
 *   uint16   version         = 1
 *   char     title[64]
 *   char     author[48]
 *   uint32   text_offset
 *   uint32   text_length
 *   uint16   chapter_count
 *   uint16   image_count
 *   Chapter  chapters[chapter_count]    // each 36 bytes
 *   Image    images[image_count]        // each 16 bytes + data
 *   byte     text[text_length]          // UTF-8, normalized
 *   byte     image_data[...]            // monochrome 1bpp tiles
 *
 * Chapter: uint32 offset; char title[32];
 * Image:   uint32 offset_in_text; uint16 w; uint16 h; uint32 data_offset; uint32 data_len;
 */

typedef struct {
    uint32_t offset;
    char     title[32];
} FBookChapter;

typedef struct {
    uint32_t offset_in_text;
    uint16_t w;
    uint16_t h;
    uint32_t data_offset;
    uint32_t data_len;
} FBookImage;

typedef struct {
    char path[256];
    char title[FBOOK_MAX_TITLE];
    char author[FBOOK_MAX_AUTHOR];

    uint32_t text_offset;
    uint32_t text_length;
    uint16_t chapter_count;
    uint16_t image_count;
    FBookChapter chapters[FBOOK_MAX_CHAPTERS];
    FBookImage   images[FBOOK_MAX_IMAGES];

    File* file;      // open handle for streamed text access
    Storage* storage;
} FBook;

FBook* fbook_alloc(void);
void fbook_free(FBook* b);

/** Open a book file. Accepts .fbook directly, or converts .epub/.txt to a cached .fbook. */
bool fbook_open(FBook* b, const char* path);

void fbook_close(FBook* b);

/** Read a chunk of decoded text from a given byte offset. Returns bytes read. */
uint32_t fbook_read(FBook* b, uint32_t offset, char* out, uint32_t max_bytes);

/** Return the 1bpp image data at images[i]; caller frees. Returns NULL on error. */
uint8_t* fbook_load_image(FBook* b, uint16_t index, uint16_t* w, uint16_t* h);

/** Find chapter index that contains a given text offset. */
uint16_t fbook_find_chapter(const FBook* b, uint32_t offset);

/** Find next image at or after a given text offset; returns image index or UINT16_MAX. */
uint16_t fbook_next_image(const FBook* b, uint32_t offset);

/** Search plain text for a needle starting at offset. Returns match offset or UINT32_MAX. */
uint32_t fbook_search(FBook* b, uint32_t start_offset, const char* needle);

/** Convert .txt file to .fbook in cache dir. Writes output path into out_path. */
bool fbook_import_txt(const char* txt_path, char* out_path, size_t out_len);

/** Best-effort in-place EPUB text extraction for "stored" (uncompressed) zip entries.
 *  For deflate-compressed content, returns false and prompts the user to pre-convert
 *  with tools/epub_to_fbook.py. */
bool fbook_import_epub(const char* epub_path, char* out_path, size_t out_len);

/** Enumerate candidate books in the library dir. Returns count copied into paths. */
uint16_t fbook_scan_library(char paths[][256], uint16_t max_paths);
