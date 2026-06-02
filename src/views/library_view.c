#include "library_view.h"

#include <furi.h>
#include <gui/canvas.h>
#include <gui/elements.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>

#define ROW_HEIGHT     16   /* fits 4 rows on a 64-row display */
#define COVER_PX       16   /* cover thumbnail drawn at 16x16 - fills row */
#define LIST_VISIBLE   4
#define HEADER_LEN     20

typedef struct {
    char path[256];
    char title[64];
    uint8_t progress_pct;
    uint32_t last_read;
    bool favorite;
    /* Cover stored at native size from fbook_peek_cover (typically 64x64); we
     * downsample at draw time so we don't have to keep two copies. NULL when
     * the book has no cover. */
    uint8_t* cover_data;
    uint16_t cover_w;
    uint16_t cover_h;
} LibraryEntry;

typedef struct {
    LibraryEntry entries[LIBRARY_VIEW_MAX];
    uint16_t count;
    uint16_t selected;
    uint16_t scroll_top;   /* index of the row drawn at the top of the screen */
    char header[HEADER_LEN];
    bool delete_mode;
    LibrarySort sort;
} LibraryModel;

struct LibraryView {
    View* view;
    LibraryViewSelectCb cb;
    void* cb_ctx;
};

/* Forward decls. */
static void render_callback(Canvas* c, void* ctx);
static bool input_callback(InputEvent* evt, void* ctx);

/* --------- helpers --------- */

static uint8_t cover_pixel(const LibraryEntry* e, uint16_t x, uint16_t y) {
    /* Sample a 1bpp LSB-first row-major bitmap. Returns 0/1. */
    if(!e->cover_data) return 0;
    if(x >= e->cover_w || y >= e->cover_h) return 0;
    uint16_t row_bytes = (e->cover_w + 7u) >> 3;
    uint8_t b = e->cover_data[(uint32_t)y * row_bytes + (x >> 3)];
    return (b >> (x & 7u)) & 1u;
}

static void draw_cover(Canvas* c, const LibraryEntry* e, int16_t dx, int16_t dy) {
    if(!e->cover_data || e->cover_w == 0 || e->cover_h == 0) {
        /* Empty placeholder: outlined box with a diagonal fill. */
        canvas_draw_frame(c, dx, dy, COVER_PX, COVER_PX);
        for(int16_t i = 1; i < COVER_PX - 1; i += 3) {
            canvas_draw_dot(c, dx + i, dy + i);
        }
        return;
    }
    /* Box-filter downsample from cover_w x cover_h (32x32 cache by default)
     * to the COVER_PX display square. For each destination pixel we look at
     * the source rectangle it represents and turn it on if at least half the
     * source pixels are set. This produces dramatically cleaner thumbnails
     * than nearest-neighbour, especially for the small detail in dust-jacket
     * lettering. */
    for(int16_t dyp = 0; dyp < COVER_PX; ++dyp) {
        uint16_t sy0 = (uint16_t)((uint32_t)dyp * e->cover_h / COVER_PX);
        uint16_t sy1 = (uint16_t)((uint32_t)(dyp + 1) * e->cover_h / COVER_PX);
        if(sy1 <= sy0) sy1 = sy0 + 1;
        if(sy1 > e->cover_h) sy1 = e->cover_h;
        for(int16_t dxp = 0; dxp < COVER_PX; ++dxp) {
            uint16_t sx0 = (uint16_t)((uint32_t)dxp * e->cover_w / COVER_PX);
            uint16_t sx1 = (uint16_t)((uint32_t)(dxp + 1) * e->cover_w / COVER_PX);
            if(sx1 <= sx0) sx1 = sx0 + 1;
            if(sx1 > e->cover_w) sx1 = e->cover_w;
            uint32_t set = 0, area = 0;
            for(uint16_t sy = sy0; sy < sy1; ++sy) {
                for(uint16_t sx = sx0; sx < sx1; ++sx) {
                    set += cover_pixel(e, sx, sy);
                    area++;
                }
            }
            if(area > 0 && set * 2 >= area) {
                canvas_draw_dot(c, dx + dxp, dy + dyp);
            }
        }
    }
}

static void draw_progress_bar_inline(Canvas* c, int16_t x, int16_t y, int16_t w, uint8_t pct) {
    if(pct > 100) pct = 100;
    canvas_draw_frame(c, x, y, w, 3);
    int16_t fill = (int16_t)((uint16_t)pct * (uint16_t)(w - 2) / 100u);
    if(fill > 0) canvas_draw_box(c, x + 1, y + 1, fill, 1);
}

static void draw_row(Canvas* c, const LibraryEntry* e, int16_t y, bool selected) {
    /* Selected row drawn in inverse so the user can see what they're on. */
    if(selected) {
        canvas_draw_box(c, 0, y, 128, ROW_HEIGHT);
        canvas_set_color(c, ColorWhite);
    } else {
        canvas_set_color(c, ColorBlack);
    }
    draw_cover(c, e, 1, y + 1);

    /* Title - clipped to width that's left after cover + progress bar. */
    canvas_set_font(c, FontSecondary);
    char tbuf[28];
    size_t n = strnlen(e->title, sizeof(e->title));
    if(n > sizeof(tbuf) - 1) n = sizeof(tbuf) - 1;
    memcpy(tbuf, e->title, n);
    tbuf[n] = '\0';
    /* Crude clip: 80px / 5px per char ~= 16 chars max. */
    if(n > 17) tbuf[17] = '\0';
    canvas_draw_str(c, COVER_PX + 4, y + 8, tbuf);

    /* Star marker for favourites, drawn just before the progress bar. */
    if(e->favorite) {
        canvas_draw_str(c, 100, y + 8, "*");
    }

    /* Inline progress bar in the bottom strip of the row. */
    draw_progress_bar_inline(c, COVER_PX + 4, y + 11, 80, e->progress_pct);

    if(selected) canvas_set_color(c, ColorBlack);
}

/* --------- view callbacks --------- */

static void render_callback(Canvas* c, void* ctx) {
    LibraryModel* m = ctx;
    canvas_clear(c);

    if(m->count == 0) {
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, 64, 24, AlignCenter, AlignCenter,
                                m->delete_mode ? "Nothing to delete" : "(no books)");
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 40, AlignCenter, AlignCenter,
                                "Drop .fbook in /library");
        return;
    }

    /* Maintain scroll_top so selection is always visible. */
    if(m->selected < m->scroll_top) m->scroll_top = m->selected;
    if(m->selected >= m->scroll_top + LIST_VISIBLE) {
        m->scroll_top = (uint16_t)(m->selected - LIST_VISIBLE + 1);
    }

    int16_t y = 0;
    for(uint16_t i = 0; i < LIST_VISIBLE; ++i) {
        uint16_t idx = m->scroll_top + i;
        if(idx >= m->count) break;
        draw_row(c, &m->entries[idx], y, idx == m->selected);
        y += ROW_HEIGHT;
    }

    /* Right-edge scrollbar so users know there are more entries. */
    if(m->count > LIST_VISIBLE) {
        int16_t bar_h = (int16_t)((uint32_t)LIST_VISIBLE * 64u / m->count);
        if(bar_h < 4) bar_h = 4;
        int16_t bar_y =
            (int16_t)((uint32_t)m->scroll_top * (64u - bar_h) / (m->count - LIST_VISIBLE));
        canvas_draw_box(c, 126, bar_y, 2, bar_h);
    }
}

static bool input_callback(InputEvent* evt, void* ctx) {
    LibraryView* v = ctx;
    bool consumed = false;
    if(evt->type != InputTypeShort && evt->type != InputTypeRepeat) return false;

    with_view_model(
        v->view, LibraryModel * m, {
            switch(evt->key) {
            case InputKeyUp:
                if(m->count > 0) {
                    m->selected = (m->selected == 0) ? (uint16_t)(m->count - 1)
                                                     : (uint16_t)(m->selected - 1);
                }
                consumed = true;
                break;
            case InputKeyDown:
                if(m->count > 0) {
                    m->selected = (uint16_t)((m->selected + 1) % m->count);
                }
                consumed = true;
                break;
            case InputKeyOk:
                /* fired below, outside the model lock */
                consumed = true;
                break;
            default:
                break;
            }
        },
        true);

    if(consumed && evt->key == InputKeyOk && v->cb) {
        uint16_t sel = 0;
        with_view_model(
            v->view, LibraryModel * m, { sel = m->selected; }, false);
        v->cb(sel, v->cb_ctx);
    }
    return consumed;
}

/* --------- public API --------- */

LibraryView* library_view_alloc(void) {
    LibraryView* v = malloc(sizeof(LibraryView));
    if(!v) return NULL;
    memset(v, 0, sizeof(*v));
    v->view = view_alloc();
    if(!v->view) {
        free(v);
        return NULL;
    }
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(LibraryModel));
    with_view_model(
        v->view, LibraryModel * m, { memset(m, 0, sizeof(*m)); }, false);
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, render_callback);
    view_set_input_callback(v->view, input_callback);
    return v;
}

static void free_covers(LibraryModel* m) {
    for(uint16_t i = 0; i < m->count; ++i) {
        if(m->entries[i].cover_data) {
            free(m->entries[i].cover_data);
            m->entries[i].cover_data = NULL;
        }
    }
}

void library_view_free(LibraryView* v) {
    if(!v) return;
    with_view_model(
        v->view, LibraryModel * m, { free_covers(m); }, false);
    if(v->view) view_free(v->view);
    free(v);
}

View* library_view_get_view(LibraryView* v) { return v ? v->view : NULL; }

void library_view_reset(LibraryView* v, const char* header, bool delete_mode) {
    if(!v || !v->view) return;
    with_view_model(
        v->view, LibraryModel * m, {
            free_covers(m);
            memset(m, 0, sizeof(*m));
            if(header) {
                strncpy(m->header, header, sizeof(m->header) - 1);
            }
            m->delete_mode = delete_mode;
        },
        true);
}

/* Downsample a source 1bpp cover into a tightly-packed 1bpp thumbnail of at
 * most COVER_CACHE_PX*COVER_CACHE_PX bytes. Returns NULL on failure or an
 * allocation the caller is expected to free on the next reset.
 *
 * Cache size 32 keeps the full v2 cover detail (the converter writes 64x64,
 * so this is one halving) while still costing only 128 bytes/entry - across
 * a 24-book library that's ~3 KB, vs the 12 KB the raw 64x64 covers cost.
 * The display itself is still a 14 px square, but a 32x32 cache gives the
 * runtime nearest-neighbour scaler enough information to avoid the splotchy
 * look 16x16 produced. */
#define COVER_CACHE_PX 32
static uint8_t* downsample_cover(const uint8_t* src, uint16_t sw, uint16_t sh) {
    if(!src || sw == 0 || sh == 0) return NULL;
    uint16_t src_rb = (uint16_t)((sw + 7u) >> 3);
    uint16_t dst_rb = (uint16_t)((COVER_CACHE_PX + 7u) >> 3);
    uint8_t* out = malloc((size_t)dst_rb * COVER_CACHE_PX);
    if(!out) return NULL;
    memset(out, 0, (size_t)dst_rb * COVER_CACHE_PX);
    /* Box-filter (count-and-threshold) downsample. For 1bpp we don't have
     * intensity, but we can preserve detail by setting a dest pixel only if
     * at least half the source pixels in its catchment area are set. The
     * old version used nearest-neighbour, which dropped any feature smaller
     * than the sample stride - including most dust-jacket lettering. */
    for(uint16_t dy = 0; dy < COVER_CACHE_PX; ++dy) {
        uint16_t sy0 = (uint16_t)((uint32_t)dy * sh / COVER_CACHE_PX);
        uint16_t sy1 = (uint16_t)((uint32_t)(dy + 1) * sh / COVER_CACHE_PX);
        if(sy1 <= sy0) sy1 = sy0 + 1;
        if(sy1 > sh) sy1 = sh;
        for(uint16_t dx = 0; dx < COVER_CACHE_PX; ++dx) {
            uint16_t sx0 = (uint16_t)((uint32_t)dx * sw / COVER_CACHE_PX);
            uint16_t sx1 = (uint16_t)((uint32_t)(dx + 1) * sw / COVER_CACHE_PX);
            if(sx1 <= sx0) sx1 = sx0 + 1;
            if(sx1 > sw) sx1 = sw;
            uint32_t set = 0, area = 0;
            for(uint16_t y = sy0; y < sy1; ++y) {
                for(uint16_t x = sx0; x < sx1; ++x) {
                    set += (src[(uint32_t)y * src_rb + (x >> 3)] >> (x & 7u)) & 1u;
                    area++;
                }
            }
            if(area > 0 && set * 2 >= area) {
                out[(uint32_t)dy * dst_rb + (dx >> 3)] |= (uint8_t)(1u << (dx & 7u));
            }
        }
    }
    return out;
}

void library_view_add_entry(
    LibraryView* v,
    const char* path,
    const char* title,
    uint8_t progress_pct,
    uint32_t last_read,
    bool favorite,
    uint8_t* cover_data,
    uint16_t cover_w,
    uint16_t cover_h) {
    if(!v || !v->view) {
        if(cover_data) free(cover_data);
        return;
    }
    /* Compress the cover into a 16x16 thumbnail before storing; the caller
     * owns the original until this function returns. */
    uint8_t* cached = NULL;
    uint16_t cached_w = 0;
    uint16_t cached_h = 0;
    if(cover_data && cover_w > 0 && cover_h > 0) {
        cached = downsample_cover(cover_data, cover_w, cover_h);
        if(cached) {
            cached_w = COVER_CACHE_PX;
            cached_h = COVER_CACHE_PX;
        }
    }
    if(cover_data) free(cover_data);

    bool stored = false;
    with_view_model(
        v->view, LibraryModel * m, {
            if(m->count < LIBRARY_VIEW_MAX) {
                LibraryEntry* e = &m->entries[m->count++];
                strncpy(e->path, path ? path : "", sizeof(e->path) - 1);
                strncpy(e->title, title ? title : "", sizeof(e->title) - 1);
                e->progress_pct = progress_pct;
                e->last_read = last_read;
                e->favorite = favorite;
                e->cover_data = cached;
                e->cover_w = cached_w;
                e->cover_h = cached_h;
                stored = true;
            }
        },
        true);
    if(!stored && cached) free(cached);
}

/* Hand-rolled insertion sort. The Flipper firmware does not expose qsort
 * through its public API (it's compiled in but symbols are stripped from
 * the FAP linker table), so we keep our own. N is at most LIBRARY_VIEW_MAX
 * (24), so O(n^2) is fine and avoids the recursion of quicksort. */
typedef int (*LibCmp)(const LibraryEntry*, const LibraryEntry*);

static int cmp_name(const LibraryEntry* a, const LibraryEntry* b) {
    return strcmp(a->title, b->title);
}
static int cmp_recent(const LibraryEntry* a, const LibraryEntry* b) {
    if(a->last_read == b->last_read) return 0;
    return b->last_read > a->last_read ? 1 : -1;
}
static int cmp_progress(const LibraryEntry* a, const LibraryEntry* b) {
    return (int)b->progress_pct - (int)a->progress_pct;
}

static void library_sort_with(LibraryEntry* arr, uint16_t n, LibCmp cmp) {
    for(uint16_t i = 1; i < n; ++i) {
        LibraryEntry tmp = arr[i];
        int16_t j = (int16_t)i - 1;
        while(j >= 0 && cmp(&arr[j], &tmp) > 0) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = tmp;
    }
}

void library_view_apply_sort(LibraryView* v, LibrarySort sort) {
    if(!v || !v->view) return;
    with_view_model(
        v->view, LibraryModel * m, {
            m->sort = sort;
            switch(sort) {
            case SortModeName:     library_sort_with(m->entries, m->count, cmp_name); break;
            case SortModeRecent:   library_sort_with(m->entries, m->count, cmp_recent); break;
            case SortModeProgress: library_sort_with(m->entries, m->count, cmp_progress); break;
            case SortModeFavoritesFirst: {
                /* Name-sorted within each partition (favourites first). */
                library_sort_with(m->entries, m->count, cmp_name);
                LibraryEntry tmp[LIBRARY_VIEW_MAX];
                uint16_t k = 0;
                for(uint16_t i = 0; i < m->count; ++i) {
                    if(m->entries[i].favorite) tmp[k++] = m->entries[i];
                }
                for(uint16_t i = 0; i < m->count; ++i) {
                    if(!m->entries[i].favorite) tmp[k++] = m->entries[i];
                }
                memcpy(m->entries, tmp, sizeof(LibraryEntry) * m->count);
                break;
            }
            default: break;
            }
            m->selected = 0;
            m->scroll_top = 0;
        },
        true);
}

const char* library_view_selected_path(LibraryView* v) {
    if(!v || !v->view) return NULL;
    static char buf[256];
    bool ok = false;
    with_view_model(
        v->view, LibraryModel * m, {
            if(m->count > 0 && m->selected < m->count) {
                strncpy(buf, m->entries[m->selected].path, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                ok = true;
            }
        },
        false);
    return ok ? buf : NULL;
}

void library_view_set_select_callback(LibraryView* v, LibraryViewSelectCb cb, void* ctx) {
    if(!v) return;
    v->cb = cb;
    v->cb_ctx = ctx;
}
