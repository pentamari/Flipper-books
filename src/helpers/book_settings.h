#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PowerModePowerSaver = 0,
    PowerModeBalanced = 1,
    PowerModeGraphics = 2,
} PowerMode;

typedef enum {
    PageAnimNone = 0,
    PageAnimSlide = 1,
    PageAnimFade = 2,
    PageAnimCurl = 3,
} PageAnimation;

typedef enum {
    TextSizeTiny = 0,
    TextSizeSmall = 1,
    TextSizeMedium = 2,
    TextSizeLarge = 3,
} TextSize;

typedef struct {
    PowerMode power_mode;
    PageAnimation page_animation;
    TextSize text_size;
    bool show_images;
    bool night_mode;
    bool auto_scroll;
    uint8_t auto_scroll_speed;   // 1..10 (seconds per page)
    uint8_t backlight_level;     // 0..100 (%)
    bool justify_text;
    bool hyphenate;
    bool show_progress_bar;
    bool vibrate_page_turn;
} BookSettings;

typedef struct {
    uint32_t total_read_seconds;
    uint32_t total_pages_read;
    uint32_t books_opened;
    uint32_t books_finished;
} BookStats;

void book_settings_set_defaults(BookSettings* s);
bool book_settings_load(BookSettings* s);
bool book_settings_save(const BookSettings* s);

void book_stats_set_defaults(BookStats* s);
bool book_stats_load(BookStats* s);
bool book_stats_save(const BookStats* s);

const char* power_mode_name(PowerMode m);
const char* page_anim_name(PageAnimation a);
const char* text_size_name(TextSize t);
uint8_t text_size_pixels(TextSize t);
