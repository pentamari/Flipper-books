#include "../books_app.h"

static void power_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.power_mode = (PowerMode)i;
    variable_item_set_current_value_text(item, power_mode_name(app->settings.power_mode));

    if(app->settings.power_mode == PowerModeGraphics) {
        notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    } else if(app->settings.power_mode == PowerModePowerSaver) {
        notification_message(app->notifications, &sequence_display_backlight_off);
    } else {
        notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
    }
}

static void anim_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.page_animation = (PageAnimation)i;
    variable_item_set_current_value_text(item, page_anim_name(app->settings.page_animation));
}

static void text_size_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.text_size = (TextSize)i;
    variable_item_set_current_value_text(item, text_size_name(app->settings.text_size));
}

static void toggle_cb_images(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.show_images = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, app->settings.show_images ? "ON" : "OFF");
}

static void toggle_cb_night(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.night_mode = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, app->settings.night_mode ? "ON" : "OFF");
}

static void toggle_cb_auto(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.auto_scroll = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, app->settings.auto_scroll ? "ON" : "OFF");
}

static void auto_speed_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.auto_scroll_speed = (uint8_t)(variable_item_get_current_value_index(item) + 1);
    char buf[8];
    snprintf(buf, sizeof(buf), "%us", app->settings.auto_scroll_speed);
    variable_item_set_current_value_text(item, buf);
}

static void backlight_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.backlight_level = (uint8_t)(i * 10);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", app->settings.backlight_level);
    variable_item_set_current_value_text(item, buf);
}

static void toggle_cb_justify(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.justify_text = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, app->settings.justify_text ? "ON" : "OFF");
}

static void toggle_cb_hyphen(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.hyphenate = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, app->settings.hyphenate ? "ON" : "OFF");
}

static void toggle_cb_pb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.show_progress_bar = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, app->settings.show_progress_bar ? "ON" : "OFF");
}

static void toggle_cb_vibe(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.vibrate_page_turn = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, app->settings.vibrate_page_turn ? "ON" : "OFF");
}

static void line_spacing_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.line_spacing = (LineSpacing)variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, line_spacing_name(app->settings.line_spacing));
}

static void font_family_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.font_family = (FontFamily)variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, font_family_name(app->settings.font_family));
}

static void margin_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.margin = (MarginSize)variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, margin_name(app->settings.margin));
}

static void toggle_page_number_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.show_page_number = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, app->settings.show_page_number ? "ON" : "OFF");
}

static const uint8_t SLEEP_PRESETS[] = {0, 5, 10, 15, 30, 60};
static void sleep_timer_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    if(i >= sizeof(SLEEP_PRESETS) / sizeof(SLEEP_PRESETS[0])) i = 0;
    app->settings.sleep_timer_minutes = SLEEP_PRESETS[i];
    char buf[12];
    if(SLEEP_PRESETS[i] == 0) {
        snprintf(buf, sizeof(buf), "OFF");
    } else {
        snprintf(buf, sizeof(buf), "%um", SLEEP_PRESETS[i]);
    }
    variable_item_set_current_value_text(item, buf);
}

static void library_sort_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.library_sort = (LibrarySort)variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, library_sort_name(app->settings.library_sort));
}

static void toggle_show_clock_cb(VariableItem* item) {
    BooksApp* app = variable_item_get_context(item);
    app->settings.show_clock = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, app->settings.show_clock ? "ON" : "OFF");
}

void books_scene_settings_on_enter(void* ctx) {
    BooksApp* app = ctx;
    VariableItemList* l = app->var_list;
    variable_item_list_reset(l);

    VariableItem* it;

    it = variable_item_list_add(l, "Power Mode", 3, power_cb, app);
    variable_item_set_current_value_index(it, app->settings.power_mode);
    variable_item_set_current_value_text(it, power_mode_name(app->settings.power_mode));

    it = variable_item_list_add(l, "Animation", 4, anim_cb, app);
    variable_item_set_current_value_index(it, app->settings.page_animation);
    variable_item_set_current_value_text(it, page_anim_name(app->settings.page_animation));

    it = variable_item_list_add(l, "Text Size", 4, text_size_cb, app);
    variable_item_set_current_value_index(it, app->settings.text_size);
    variable_item_set_current_value_text(it, text_size_name(app->settings.text_size));

    it = variable_item_list_add(l, "Images", 2, toggle_cb_images, app);
    variable_item_set_current_value_index(it, app->settings.show_images ? 1 : 0);
    variable_item_set_current_value_text(it, app->settings.show_images ? "ON" : "OFF");

    it = variable_item_list_add(l, "Night Mode", 2, toggle_cb_night, app);
    variable_item_set_current_value_index(it, app->settings.night_mode ? 1 : 0);
    variable_item_set_current_value_text(it, app->settings.night_mode ? "ON" : "OFF");

    it = variable_item_list_add(l, "Auto-Scroll", 2, toggle_cb_auto, app);
    variable_item_set_current_value_index(it, app->settings.auto_scroll ? 1 : 0);
    variable_item_set_current_value_text(it, app->settings.auto_scroll ? "ON" : "OFF");

    it = variable_item_list_add(l, "Scroll Speed", 10, auto_speed_cb, app);
    variable_item_set_current_value_index(it, app->settings.auto_scroll_speed - 1);
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%us", app->settings.auto_scroll_speed);
        variable_item_set_current_value_text(it, buf);
    }

    it = variable_item_list_add(l, "Backlight", 11, backlight_cb, app);
    variable_item_set_current_value_index(it, app->settings.backlight_level / 10);
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", app->settings.backlight_level);
        variable_item_set_current_value_text(it, buf);
    }

    it = variable_item_list_add(l, "Justify", 2, toggle_cb_justify, app);
    variable_item_set_current_value_index(it, app->settings.justify_text ? 1 : 0);
    variable_item_set_current_value_text(it, app->settings.justify_text ? "ON" : "OFF");

    it = variable_item_list_add(l, "Hyphenate", 2, toggle_cb_hyphen, app);
    variable_item_set_current_value_index(it, app->settings.hyphenate ? 1 : 0);
    variable_item_set_current_value_text(it, app->settings.hyphenate ? "ON" : "OFF");

    it = variable_item_list_add(l, "Progress Bar", 2, toggle_cb_pb, app);
    variable_item_set_current_value_index(it, app->settings.show_progress_bar ? 1 : 0);
    variable_item_set_current_value_text(it, app->settings.show_progress_bar ? "ON" : "OFF");

    it = variable_item_list_add(l, "Vibrate", 2, toggle_cb_vibe, app);
    variable_item_set_current_value_index(it, app->settings.vibrate_page_turn ? 1 : 0);
    variable_item_set_current_value_text(it, app->settings.vibrate_page_turn ? "ON" : "OFF");

    /* New in fbook2: layout & UX options. */
    it = variable_item_list_add(l, "Line Spacing", 4, line_spacing_cb, app);
    variable_item_set_current_value_index(it, app->settings.line_spacing);
    variable_item_set_current_value_text(it, line_spacing_name(app->settings.line_spacing));

    it = variable_item_list_add(l, "Font", 3, font_family_cb, app);
    variable_item_set_current_value_index(it, app->settings.font_family);
    variable_item_set_current_value_text(it, font_family_name(app->settings.font_family));

    it = variable_item_list_add(l, "Margin", 3, margin_cb, app);
    variable_item_set_current_value_index(it, app->settings.margin);
    variable_item_set_current_value_text(it, margin_name(app->settings.margin));

    it = variable_item_list_add(l, "Page Number", 2, toggle_page_number_cb, app);
    variable_item_set_current_value_index(it, app->settings.show_page_number ? 1 : 0);
    variable_item_set_current_value_text(it, app->settings.show_page_number ? "ON" : "OFF");

    it = variable_item_list_add(l, "Show Clock", 2, toggle_show_clock_cb, app);
    variable_item_set_current_value_index(it, app->settings.show_clock ? 1 : 0);
    variable_item_set_current_value_text(it, app->settings.show_clock ? "ON" : "OFF");

    {
        uint8_t sleep_idx = 0;
        for(uint8_t i = 0; i < sizeof(SLEEP_PRESETS) / sizeof(SLEEP_PRESETS[0]); ++i) {
            if(SLEEP_PRESETS[i] == app->settings.sleep_timer_minutes) {
                sleep_idx = i;
                break;
            }
        }
        it = variable_item_list_add(l, "Sleep Timer",
                                    sizeof(SLEEP_PRESETS) / sizeof(SLEEP_PRESETS[0]),
                                    sleep_timer_cb, app);
        variable_item_set_current_value_index(it, sleep_idx);
        char buf[12];
        if(app->settings.sleep_timer_minutes == 0) {
            snprintf(buf, sizeof(buf), "OFF");
        } else {
            snprintf(buf, sizeof(buf), "%um", app->settings.sleep_timer_minutes);
        }
        variable_item_set_current_value_text(it, buf);
    }

    it = variable_item_list_add(l, "Library Sort", 4, library_sort_cb, app);
    variable_item_set_current_value_index(it, app->settings.library_sort);
    variable_item_set_current_value_text(it, library_sort_name(app->settings.library_sort));

    view_dispatcher_switch_to_view(app->view_dispatcher, BooksViewVarList);
}

bool books_scene_settings_on_event(void* ctx, SceneManagerEvent event) {
    UNUSED(ctx);
    UNUSED(event);
    return false;
}

void books_scene_settings_on_exit(void* ctx) {
    BooksApp* app = ctx;
    book_settings_save(&app->settings);
    variable_item_list_reset(app->var_list);
}
