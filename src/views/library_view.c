#include "library_view.h"

#include <furi.h>
#include <gui/canvas.h>
#include <gui/elements.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>

#define ROW_HEIGHT     16   /* fits 4 rows on a 64-row display */
#define COVER_PX       14   /* cover thumbnail drawn at 14x14 */
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
    /* Box outline first - gives books without cover art a placeholder. */
    canvas_draw_frame(c, dx, dy, COVER_PX, COVER_PX);
    if(!e->cover_data || e->cover_w == 0 || e->cover_h == 0) {
        /* Diagonal fill so empty covers are obvious. */
        for(int16_t i = 1; i < COVER_PX - 1; i += 3) {
            canvas_draw_dot(c, dx + i, dy + i);
        }
        return;
    }
    /* Nearest-neighbour scale from native cover_w/h to COVER_PX. */
    for(int16_t y = 0; y < COVER_PX - 2; y++) {
        for(int16_t x = 0; x < COVER_PX - 2; x++) {
            uint16_t sx = (uint16_t)((uint32_t)x * e->cover_w / (COVER_PX - 2));
            uint16_t sy = (uint16_t)((uint32_t)y * e->cover_h / (COVER_PX - 2));
            if(cover_pixel(e, sx, sy)) {
                canvas_draw_dot(c, dx + 1 + x, dy + 1 + y);
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
                if(m->selected > 0) m->selected--;
                consumed = true;
                break;
            case InputKeyDown:
                if(m->selected + 1 < m->count) m->selected++;
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
    memset(v, 0, sizeof(*v));
    v->view = view_alloc();
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
    view_free(v->view);
    free(v);
}

View* library_view_get_view(LibraryView* v) { return v->view; }

void library_view_reset(LibraryView* v, const char* header, bool delete_mode) {
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
                e->cover_data = cover_data;
                e->cover_w = cover_w;
                e->cover_h = cover_h;
                stored = true;
            }
        },
        true);
    if(!stored && cover_data) free(cover_data);
}

static int cmp_name(const void* a, const void* b) {
    return strcmp(((LibraryEntry*)a)->title, ((LibraryEntry*)b)->title);
}
static int cmp_recent(const void* a, const void* b) {
    uint32_t la = ((LibraryEntry*)a)->last_read;
    uint32_t lb = ((LibraryEntry*)b)->last_read;
    if(la == lb) return 0;
    return lb > la ? 1 : -1;
}
static int cmp_progress(const void* a, const void* b) {
    int pa = ((LibraryEntry*)a)->progress_pct;
    int pb = ((LibraryEntry*)b)->progress_pct;
    return pb - pa;
}

void library_view_apply_sort(LibraryView* v, LibrarySort sort) {
    with_view_model(
        v->view, LibraryModel * m, {
            m->sort = sort;
            switch(sort) {
            case SortModeName:     qsort(m->entries, m->count, sizeof(LibraryEntry), cmp_name); break;
            case SortModeRecent:   qsort(m->entries, m->count, sizeof(LibraryEntry), cmp_recent); break;
            case SortModeProgress: qsort(m->entries, m->count, sizeof(LibraryEntry), cmp_progress); break;
            case SortModeFavoritesFirst: {
                /* In-place partition: favourites first, name-sorted within
                 * each partition. */
                qsort(m->entries, m->count, sizeof(LibraryEntry), cmp_name);
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
    v->cb = cb;
    v->cb_ctx = ctx;
}
