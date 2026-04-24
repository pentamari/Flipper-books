#include "reader_view.h"

#include <furi.h>
#include <gui/elements.h>
#include <input/input.h>
#include <string.h>
#include <stdlib.h>

#define READER_W 128
#define READER_H 64

#define PAGE_STACK 64

typedef struct {
    uint32_t offsets[PAGE_STACK];
    uint16_t count;
} PageStack;

struct ReaderView {
    View* view;
    ReaderEventCallback event_cb;
    void* event_ctx;
    FuriTimer* anim_timer;
    FuriTimer* auto_timer;
};

typedef struct {
    FBook* book;
    const BookSettings* settings;
    BookProgress progress; // copied in

    uint32_t page_offset;      // start of current page
    uint32_t page_end_offset;  // start of next page
    uint32_t page_number;

    PageStack stack;

    // animation
    int8_t anim_dir; // -1 back, +1 forward, 0 none
    uint8_t anim_progress; // 0..100

    // small draw cache of visible lines
    char cache[512];
    uint16_t cache_len;

    // status
    bool show_bookmark_flash;
    uint32_t bookmark_flash_until;
    bool menu_prompt;
} ReaderModel;

static Font font_for_size(TextSize t) {
    switch(t) {
    case TextSizeTiny:
    case TextSizeSmall:
        return FontSecondary;
    case TextSizeMedium:
        return FontPrimary;
    case TextSizeLarge:
        return FontPrimary;
    }
    return FontSecondary;
}

static uint8_t line_height_for_size(TextSize t) {
    switch(t) {
    case TextSizeTiny:   return 7;
    case TextSizeSmall:  return 9;
    case TextSizeMedium: return 11;
    case TextSizeLarge:  return 14;
    }
    return 9;
}

static uint8_t max_chars_per_line(TextSize t) {
    switch(t) {
    case TextSizeTiny:   return 32;
    case TextSizeSmall:  return 26;
    case TextSizeMedium: return 19;
    case TextSizeLarge:  return 14;
    }
    return 26;
}

/* Height in pixels reserved at the top of the page for an inline image on the
 * current page, matching what draw_image_on_page will actually render. */
static uint8_t image_top_reserve(const ReaderModel* m) {
    if(!m->settings->show_images) return 0;
    if(m->settings->power_mode == PowerModePowerSaver) return 0;
    if(!m->book || m->book->image_count == 0) return 0;
    uint16_t idx = fbook_next_image(m->book, m->page_offset);
    if(idx == UINT16_MAX) return 0;
    uint32_t off = m->book->images[idx].offset_in_text;
    if(off < m->page_offset) return 0;
    if(off >= m->page_end_offset) return 0;
    uint16_t h = m->book->images[idx].h;
    if(h > 32) h = 32;
    return (uint8_t)h;
}

/* Compute page end offset by word-wrapping in the cache. */
static void compute_page_end(ReaderModel* m) {
    m->cache_len = fbook_read(m->book, m->page_offset, m->cache, sizeof(m->cache) - 1);
    m->cache[m->cache_len] = '\0';

    uint8_t lh = line_height_for_size(m->settings->text_size);
    uint8_t avail_h = READER_H;
    if(m->settings->show_progress_bar && avail_h > 3) avail_h -= 3;

    /* Reserve room for an inline image on this page, using cache_len as the
     * upper bound for what this page can cover. We also subtract 2px of gap
     * between the image and the first line of text so descenders don't touch. */
    uint8_t reserve = 0;
    if(m->settings->show_images && m->settings->power_mode != PowerModePowerSaver) {
        uint16_t img_idx = fbook_next_image(m->book, m->page_offset);
        if(img_idx != UINT16_MAX &&
           m->book->images[img_idx].offset_in_text < m->page_offset + m->cache_len) {
            uint16_t h = m->book->images[img_idx].h;
            if(h > 32) h = 32;
            reserve = (uint8_t)h;
            if(reserve + 2 < avail_h) reserve += 2;
        }
    }
    uint8_t text_h = avail_h > reserve ? avail_h - reserve : lh;
    uint8_t max_lines = (uint8_t)(text_h / lh);
    if(max_lines < 1) max_lines = 1;

    uint8_t mcpl = max_chars_per_line(m->settings->text_size);

    uint16_t i = 0;
    uint16_t lines = 0;
    uint16_t last_break = 0;

    while(i < m->cache_len && lines < max_lines) {
        // newline handling (hard break)
        uint16_t line_start = i;
        uint16_t col = 0;
        while(i < m->cache_len && m->cache[i] != '\n' && col < mcpl) {
            i++;
            col++;
        }
        if(i < m->cache_len && m->cache[i] == '\n') {
            i++;
        } else if(col == mcpl && i < m->cache_len) {
            // back up to previous space for word wrap
            uint16_t j = i;
            while(j > line_start && m->cache[j] != ' ') j--;
            if(j > line_start) i = j + 1;
        }
        last_break = i;
        lines++;
    }

    if(last_break == 0) last_break = m->cache_len; // fallback, prevents infinite loop
    m->page_end_offset = m->page_offset + last_break;
    if(m->page_end_offset > m->book->text_length) m->page_end_offset = m->book->text_length;
}

static void push_page(PageStack* s, uint32_t offset) {
    if(s->count < PAGE_STACK) {
        s->offsets[s->count++] = offset;
    } else {
        for(int i = 0; i < PAGE_STACK - 1; ++i) s->offsets[i] = s->offsets[i + 1];
        s->offsets[PAGE_STACK - 1] = offset;
    }
}

static bool pop_page(PageStack* s, uint32_t* out) {
    if(s->count == 0) return false;
    *out = s->offsets[--s->count];
    return true;
}

static void draw_night(Canvas* c, const BookSettings* s) {
    if(s->night_mode) {
        /* Fill the background with pixels-on so subsequent ColorWhite
         * (pixels-off) draws show through as light text on a dark page. */
        canvas_set_color(c, ColorBlack);
        canvas_draw_box(c, 0, 0, READER_W, READER_H);
        canvas_set_color(c, ColorWhite);
    }
}

static void draw_text_page(Canvas* c, ReaderModel* m, int16_t x_offset) {
    canvas_set_font(c, font_for_size(m->settings->text_size));
    uint8_t lh = line_height_for_size(m->settings->text_size);
    uint8_t mcpl = max_chars_per_line(m->settings->text_size);

    uint8_t reserve = image_top_reserve(m);
    if(reserve > 0 && reserve + 2 < READER_H) reserve += 2; // breathing room

    uint16_t i = 0;
    uint16_t lines = 0;
    int16_t y = reserve + lh - 1;
    uint8_t avail_h = READER_H > reserve ? READER_H - reserve : lh;
    if(m->settings->show_progress_bar && avail_h > 3) avail_h -= 3;
    uint8_t max_lines = (uint8_t)(avail_h / lh);
    if(max_lines < 1) max_lines = 1;

    char line[40];
    uint16_t line_end = m->page_end_offset - m->page_offset;
    if(line_end > sizeof(m->cache) - 1) line_end = sizeof(m->cache) - 1;

    while(i < line_end && lines < max_lines) {
        uint16_t line_start = i;
        uint16_t col = 0;
        while(i < line_end && m->cache[i] != '\n' && col < mcpl) {
            i++;
            col++;
        }
        uint16_t end = i;
        bool hard_nl = false;
        if(i < line_end && m->cache[i] == '\n') {
            hard_nl = true;
            i++;
        } else if(col == mcpl && i < line_end) {
            uint16_t j = i;
            while(j > line_start && m->cache[j] != ' ') j--;
            if(j > line_start) {
                end = j;
                i = j + 1;
            }
        }
        uint16_t n = end - line_start;
        if(n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, &m->cache[line_start], n);
        line[n] = '\0';
        // strip trailing carriage return if any
        while(n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) {
            line[--n] = '\0';
        }
        if(m->settings->night_mode) canvas_set_color(c, ColorWhite);
        else canvas_set_color(c, ColorBlack);
        canvas_draw_str(c, 1 + x_offset, y, line);
        y += lh;
        lines++;
        if(!hard_nl && i < line_end && m->cache[i] == ' ') i++;
    }
}

static void draw_progress_bar(Canvas* c, const FBook* b, uint32_t offset, bool night) {
    if(b->text_length == 0) return;
    uint32_t pct = (offset * 100) / b->text_length;
    if(pct > 100) pct = 100;
    int16_t w = (int16_t)((pct * (READER_W - 2)) / 100);
    if(night) canvas_set_color(c, ColorWhite);
    else canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, 0, READER_H - 2, READER_W, 2);
    canvas_draw_box(c, 1, READER_H - 2, w, 2);
}

static void draw_image_on_page(Canvas* c, ReaderModel* m) {
    if(!m->settings->show_images) return;
    if(m->settings->power_mode == PowerModePowerSaver) return;
    uint16_t idx = fbook_next_image(m->book, m->page_offset);
    if(idx == UINT16_MAX) return;
    if(m->book->images[idx].offset_in_text >= m->page_end_offset) return;
    if(m->book->images[idx].offset_in_text < m->page_offset) return;

    /* Guard against corrupt dimensions before we ask the loader to malloc and
     * read (w*h)/8 bytes for us. A full-page image at this resolution is at
     * most READER_W * 32 / 8 = 512 bytes. */
    uint16_t stored_w = m->book->images[idx].w;
    uint16_t stored_h = m->book->images[idx].h;
    if(stored_w == 0 || stored_h == 0) return;
    if(stored_w > READER_W || stored_h > READER_H) return;

    uint16_t w, h;
    uint8_t fmt = 0;
    uint8_t* data = fbook_load_image(m->book, idx, &w, &h, &fmt);
    if(!data) return;
    (void)fmt; /* 2bpp rendering is added below; for now treat all as XBM. */

    uint16_t draw_w = w > READER_W ? READER_W : w;
    uint16_t draw_h = h > 32 ? 32 : h;
    int16_t x = (READER_W - draw_w) / 2;
    int16_t y = 0;
    canvas_draw_xbm(c, x, y, draw_w, draw_h, data);
    free(data);
}

static void render_callback(Canvas* c, void* ctx) {
    ReaderModel* m = ctx;
    canvas_clear(c);
    draw_night(c, m->settings);

    if(!m->book || !m->book->file) {
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, READER_W / 2, READER_H / 2, AlignCenter, AlignCenter, "No book");
        return;
    }

    if(m->anim_dir != 0 &&
       m->settings->page_animation != PageAnimNone &&
       m->settings->power_mode == PowerModeGraphics) {
        int16_t shift = (int16_t)((int32_t)m->anim_progress * READER_W / 100) * m->anim_dir;
        draw_text_page(c, m, -shift);
    } else {
        draw_text_page(c, m, 0);
    }

    draw_image_on_page(c, m);

    if(m->settings->show_progress_bar) {
        draw_progress_bar(c, m->book, m->page_offset, m->settings->night_mode);
    }

    if(m->show_bookmark_flash && furi_get_tick() < m->bookmark_flash_until) {
        canvas_set_color(c, m->settings->night_mode ? ColorWhite : ColorBlack);
        canvas_draw_box(c, READER_W - 8, 0, 6, 8);
    } else {
        m->show_bookmark_flash = false;
    }
}

static void anim_timer_cb(void* ctx) {
    ReaderView* r = ctx;
    with_view_model(
        r->view, ReaderModel * m, {
            if(m->anim_dir != 0) {
                if(m->anim_progress >= 100) {
                    m->anim_dir = 0;
                    m->anim_progress = 0;
                } else {
                    m->anim_progress = (uint8_t)(m->anim_progress + 20);
                    if(m->anim_progress > 100) m->anim_progress = 100;
                }
            }
        },
        true);
}

static void auto_timer_cb(void* ctx) {
    ReaderView* r = ctx;
    with_view_model(
        r->view, ReaderModel * m, {
            if(m->settings->auto_scroll && m->page_end_offset < m->book->text_length) {
                push_page(&m->stack, m->page_offset);
                m->page_offset = m->page_end_offset;
                m->page_number++;
                compute_page_end(m);
                m->anim_dir = 1;
                m->anim_progress = 0;
            }
        },
        true);
}

static void emit_event(ReaderView* r, ReaderPublicEvent ev) {
    if(r->event_cb) r->event_cb(ev, r->event_ctx);
}

static bool input_callback(InputEvent* evt, void* ctx) {
    ReaderView* r = ctx;
    bool consumed = false;

    if(evt->type == InputTypeShort) {
        with_view_model(
            r->view, ReaderModel * m, {
                switch(evt->key) {
                case InputKeyRight:
                    if(m->page_end_offset < m->book->text_length) {
                        push_page(&m->stack, m->page_offset);
                        m->page_offset = m->page_end_offset;
                        m->page_number++;
                        compute_page_end(m);
                        m->anim_dir = 1;
                        m->anim_progress = 0;
                    }
                    consumed = true;
                    break;
                case InputKeyLeft: {
                    uint32_t prev;
                    if(pop_page(&m->stack, &prev)) {
                        m->page_offset = prev;
                        if(m->page_number > 0) m->page_number--;
                        compute_page_end(m);
                        m->anim_dir = -1;
                        m->anim_progress = 0;
                    }
                    consumed = true;
                    break;
                }
                case InputKeyUp:
                    // emit bookmark
                    m->show_bookmark_flash = true;
                    m->bookmark_flash_until = furi_get_tick() + 800;
                    consumed = true;
                    break;
                case InputKeyDown:
                    // search next
                    break;
                case InputKeyOk:
                    break;
                default: break;
                }
            },
            true);

        // events outside the model
        if(evt->key == InputKeyOk) {
            emit_event(r, ReaderEventMenu);
            consumed = true;
        } else if(evt->key == InputKeyBack) {
            emit_event(r, ReaderEventBack);
            consumed = true;
        } else if(evt->key == InputKeyUp) {
            emit_event(r, ReaderEventBookmark);
            consumed = true;
        } else if(evt->key == InputKeyDown) {
            emit_event(r, ReaderEventToc);
            consumed = true;
        }
    }

    return consumed;
}

ReaderView* reader_view_alloc(void) {
    ReaderView* r = malloc(sizeof(ReaderView));
    memset(r, 0, sizeof(*r));
    r->view = view_alloc();
    view_allocate_model(r->view, ViewModelTypeLocking, sizeof(ReaderModel));
    with_view_model(
        r->view, ReaderModel * m, { memset(m, 0, sizeof(*m)); }, false);
    view_set_context(r->view, r);
    view_set_draw_callback(r->view, render_callback);
    view_set_input_callback(r->view, input_callback);

    r->anim_timer = furi_timer_alloc(anim_timer_cb, FuriTimerTypePeriodic, r);
    furi_timer_start(r->anim_timer, furi_ms_to_ticks(30));
    r->auto_timer = furi_timer_alloc(auto_timer_cb, FuriTimerTypePeriodic, r);
    return r;
}

void reader_view_free(ReaderView* r) {
    if(!r) return;
    furi_timer_stop(r->anim_timer);
    furi_timer_free(r->anim_timer);
    furi_timer_stop(r->auto_timer);
    furi_timer_free(r->auto_timer);
    view_free(r->view);
    free(r);
}

View* reader_view_get_view(ReaderView* r) { return r->view; }

void reader_view_set_book(ReaderView* r, FBook* book) {
    with_view_model(
        r->view, ReaderModel * m, {
            m->book = book;
            m->page_offset = 0;
            m->page_end_offset = 0;
            m->page_number = 0;
            m->stack.count = 0;
            if(book && m->settings) compute_page_end(m);
        },
        true);
}

void reader_view_set_settings(ReaderView* r, const BookSettings* settings) {
    with_view_model(
        r->view, ReaderModel * m, {
            m->settings = settings;
            if(settings->auto_scroll) {
                furi_timer_start(r->auto_timer, furi_ms_to_ticks(1000u * settings->auto_scroll_speed));
            } else {
                furi_timer_stop(r->auto_timer);
            }
            if(m->book) compute_page_end(m);
        },
        true);
}

void reader_view_set_progress(ReaderView* r, const BookProgress* progress) {
    with_view_model(
        r->view, ReaderModel * m, {
            m->progress = *progress;
            m->page_offset = progress->offset;
            m->page_number = progress->page;
            m->stack.count = 0;
            if(m->book && m->settings) compute_page_end(m);
        },
        true);
}

uint32_t reader_view_get_offset(const ReaderView* r) {
    uint32_t out = 0;
    with_view_model(
        r->view, ReaderModel * m, { out = m->page_offset; }, false);
    return out;
}

uint32_t reader_view_get_page(const ReaderView* r) {
    uint32_t out = 0;
    with_view_model(
        r->view, ReaderModel * m, { out = m->page_number; }, false);
    return out;
}

void reader_view_jump_to(ReaderView* r, uint32_t offset) {
    with_view_model(
        r->view, ReaderModel * m, {
            push_page(&m->stack, m->page_offset);
            m->page_offset = offset;
            if(m->book && m->settings) compute_page_end(m);
        },
        true);
}

void reader_view_set_event_callback(ReaderView* r, ReaderEventCallback cb, void* ctx) {
    r->event_cb = cb;
    r->event_ctx = ctx;
}
