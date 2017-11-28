#include <pebble.h>

#define PERSIST_COUNTDOWN_EXPIRE 1
#define PERSIST_COUNTDOWN_PAUSED 2
#define PERSIST_IN_REST_MODE 3
#define PERSIST_WORK_INTERVAL 10
#define PERSIST_REST_INTERVAL 20
#define PERSIST_OVERRUNABLE 40

#define DEFAULT_WORK_INTERVAL 1800
#define DEFAULT_REST_INTERVAL 300
#define DEFAULT_OVERRUNABLE true

#define NUM_MENU_SECTIONS 1
#define NUM_MENU_ITEMS 4

#define MAX_WORK_INTERVAL 3600
#define WORK_INTERVAL_INCREMENT 300

#define MAX_REST_INTERVAL 600
#define REST_INTERVAL_INCREMENT 60

#define TIME_STR_LENGTH 6

static int config_work_interval;
static int config_rest_interval;
static bool config_overrunable;

static bool config_changed;

static Window *s_main_window;
static Window *s_menu_window;

static TextLayer *s_clock_layer;
static TextLayer *s_countdown_layer;
static TextLayer *s_progressbar_layer;

static SimpleMenuLayer *s_simple_menu_layer;
static SimpleMenuSection s_menu_sections[NUM_MENU_SECTIONS];
static SimpleMenuItem s_menu_items[NUM_MENU_ITEMS];

// 3 key states
static bool s_in_rest_mode = false;
static bool s_countdown_paused = true;
static int s_countdown_seconds;

static void init_settings() {
    config_work_interval = (
        persist_exists(PERSIST_WORK_INTERVAL) ?
            persist_read_int(PERSIST_WORK_INTERVAL) :
            DEFAULT_WORK_INTERVAL
    );
    config_rest_interval = (
        persist_exists(PERSIST_REST_INTERVAL) ?
            persist_read_int(PERSIST_REST_INTERVAL) :
            DEFAULT_REST_INTERVAL
    );
    config_overrunable = (
        persist_exists(PERSIST_OVERRUNABLE) ?
            persist_read_bool(PERSIST_OVERRUNABLE) :
            DEFAULT_OVERRUNABLE
    );
    s_in_rest_mode = (
        persist_exists(PERSIST_IN_REST_MODE) ?
            persist_read_bool(PERSIST_IN_REST_MODE) :
            false
    );
    s_countdown_paused = (
        persist_exists(PERSIST_COUNTDOWN_PAUSED) ?
            persist_read_bool(PERSIST_COUNTDOWN_PAUSED) :
            true
    );
    s_countdown_seconds = (
        persist_exists(PERSIST_COUNTDOWN_EXPIRE) ?
            persist_read_int(PERSIST_COUNTDOWN_EXPIRE) :
            config_work_interval
    );
    if (s_countdown_seconds > 10000000) {
        s_countdown_seconds -= time(NULL);
    }
}

static char* format_time(int time, char* str) {
    time = abs(time);
    int seconds = time % 60;
    int minutes = time / 60;
    snprintf(str, TIME_STR_LENGTH, "%01d:%02d", minutes, seconds);
    return str;
}

static void menu_update_work_interval(int index, void *context) {
    static char time_str[TIME_STR_LENGTH];

    if (config_work_interval == MAX_WORK_INTERVAL) {
        config_work_interval = WORK_INTERVAL_INCREMENT;
    } else {
        config_work_interval += WORK_INTERVAL_INCREMENT;
    }

    s_menu_items[0].subtitle = format_time(
        config_work_interval,
        time_str
    );

    config_changed = true;
    layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
}

static void menu_update_rest_interval(int index, void *context) {
    static char time_str[TIME_STR_LENGTH];

    if (config_rest_interval == MAX_REST_INTERVAL) {
        config_rest_interval = REST_INTERVAL_INCREMENT;
    } else {
        config_rest_interval += REST_INTERVAL_INCREMENT;
    }

    s_menu_items[1].subtitle = format_time(
        config_rest_interval,
        time_str
    );

    config_changed = true;
    layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
}

static void menu_update_overrun(int index, void *context) {
    config_overrunable = !config_overrunable;

    s_menu_items[2].subtitle = config_overrunable ? "On" : "Off";

    layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
}

static void build_menu() {
    static char work_countdown_str[TIME_STR_LENGTH];
    static char rest_countdown_str[TIME_STR_LENGTH];

    s_menu_items[0] = (SimpleMenuItem) {
        .title = "Work Interval",
        .subtitle = format_time(config_work_interval, work_countdown_str),
        .callback = menu_update_work_interval
    };
    s_menu_items[1] = (SimpleMenuItem) {
        .title = "Rest Interval",
        .subtitle = format_time(config_rest_interval, rest_countdown_str),
        .callback = menu_update_rest_interval
    };
    s_menu_items[2] = (SimpleMenuItem) {
        .title = "Overrun",
        .subtitle = config_overrunable ? "On" : "Off",
        .callback = menu_update_overrun
    };
    s_menu_sections[0] = (SimpleMenuSection) {
        .title = "Settings",
        .num_items = NUM_MENU_ITEMS,
        .items = s_menu_items
    };
}

static void set_colors() {
    if (s_in_rest_mode) {
        window_set_background_color(s_main_window, GColorGreen);

        text_layer_set_background_color(s_clock_layer, GColorGreen);
        text_layer_set_text_color(s_clock_layer, GColorBlack );

        text_layer_set_background_color(s_countdown_layer, GColorGreen);
        text_layer_set_text_color(s_countdown_layer, GColorBlack );
    } else {
        window_set_background_color(s_main_window, GColorBlueMoon);

        text_layer_set_background_color(s_clock_layer, GColorBlueMoon);
        text_layer_set_text_color(s_clock_layer, GColorWhite);

        text_layer_set_background_color(s_countdown_layer, GColorBlueMoon);
        text_layer_set_text_color(s_countdown_layer, GColorWhite);
    }

    if (s_countdown_seconds < 0) {
        text_layer_set_background_color(s_progressbar_layer, GColorRed );
        text_layer_set_text_color(s_progressbar_layer, GColorWhite);
    } else {
        text_layer_set_background_color(s_progressbar_layer, GColorWhite);
        text_layer_set_text_color(s_progressbar_layer, GColorBlack);
    }
}

static void update_clock_time() {
    time_t temp = time(NULL);
    struct tm *tick_time = localtime(&temp);
    static char *buffer = "00:00";
    if (clock_is_24h_style() == true) {
        strftime(buffer, TIME_STR_LENGTH, "%H:%M", tick_time);
    } else {
        strftime(buffer, TIME_STR_LENGTH, "%I:%M", tick_time);
    }
    text_layer_set_text(s_clock_layer, buffer);
}

static void update_countdown() {
    static char countdown_str[TIME_STR_LENGTH];
    text_layer_set_text(
        s_countdown_layer,
        format_time(s_countdown_seconds, countdown_str)
    );
}

static void update_progressbar() {
    if (s_countdown_paused) {
        int interval = s_in_rest_mode ? config_rest_interval : config_work_interval;
        if (s_countdown_seconds == interval) {
            text_layer_set_text(s_progressbar_layer, "Ready");
        } else {
            text_layer_set_text(s_progressbar_layer, "Paused");
        }
    } else {
        if (s_countdown_seconds < 0) {
            text_layer_set_text(s_progressbar_layer, "Overrun");
            text_layer_set_background_color(s_progressbar_layer, GColorRed );
            text_layer_set_text_color(s_progressbar_layer, GColorWhite);
        } else {
            text_layer_set_text(s_progressbar_layer, "");
        }
    }
}

static void main_window_refresh() {
    set_colors();
    update_clock_time();
    update_countdown();
    update_progressbar();
}

static void start_mode(bool is_rest_mode) {
    s_in_rest_mode = is_rest_mode;
    if (is_rest_mode) {
        s_countdown_seconds = config_rest_interval;
        vibes_double_pulse();
    } else {
        s_countdown_seconds = config_work_interval;
        vibes_short_pulse();
    }
    s_countdown_paused = false;
    main_window_refresh();
}

static void time_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    if (units_changed | MINUTE_UNIT) {
        update_clock_time();
    }

    if (s_countdown_paused) {
        return;
    }

    --s_countdown_seconds;
    update_countdown();
    update_progressbar();

    if (s_countdown_seconds <= 0 && !config_overrunable) {
        start_mode(!s_in_rest_mode);
    } else if (s_countdown_seconds <= 0 && s_countdown_seconds % 60 == 0) {
        vibes_short_pulse();
    }
}

static void persist_config() {
    persist_write_int(PERSIST_WORK_INTERVAL, config_work_interval);
    persist_write_int(PERSIST_REST_INTERVAL, config_rest_interval);
    persist_write_bool(PERSIST_OVERRUNABLE, config_overrunable);
}

static void persist_status() {
    persist_write_int(
        PERSIST_COUNTDOWN_EXPIRE,
        s_countdown_paused ? s_countdown_seconds : time(NULL) + s_countdown_seconds);
    persist_write_bool(PERSIST_COUNTDOWN_PAUSED, s_countdown_paused);
    persist_write_bool(PERSIST_IN_REST_MODE, s_in_rest_mode);
}

static void main_window_load(Window *window) {
    s_countdown_layer = text_layer_create(GRect(5, 23, 144, 36));
    s_progressbar_layer = text_layer_create(GRect(0, 64, 144, 32));
    s_clock_layer = text_layer_create(GRect(5, 102, 144, 50));

    text_layer_set_font(s_clock_layer, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS));
    text_layer_set_font(s_countdown_layer, fonts_get_system_font(FONT_KEY_LECO_28_LIGHT_NUMBERS));
    text_layer_set_font(s_progressbar_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));

    text_layer_set_text_alignment(s_progressbar_layer, GTextAlignmentCenter);

    Layer* root_layer = window_get_root_layer(window);

    layer_add_child(root_layer, text_layer_get_layer(s_clock_layer));
    layer_add_child(root_layer, text_layer_get_layer(s_countdown_layer));
    layer_add_child(root_layer, text_layer_get_layer(s_progressbar_layer));
}

static void main_window_unload(Window *window) {
    text_layer_destroy(s_clock_layer);
    text_layer_destroy(s_countdown_layer);
    text_layer_destroy(s_progressbar_layer);
}

static void menu_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_frame(window_layer);

    config_changed = false;

    s_simple_menu_layer = simple_menu_layer_create(
        bounds,
        window,
        s_menu_sections,
        NUM_MENU_SECTIONS,
        NULL
    );

    layer_add_child(
        window_layer,
        simple_menu_layer_get_layer(s_simple_menu_layer)
    );
}

static void menu_window_unload(Window *window) {
    persist_config();

    if ( config_changed ) {
        s_countdown_paused = true;
        s_countdown_seconds = config_work_interval;
        s_in_rest_mode = false;
    }
    simple_menu_layer_destroy(s_simple_menu_layer);
}

static void up_single_click_handler (ClickRecognizerRef recognizer,
                                     void *context) {
    start_mode(true);
}

static void down_single_click_handler (ClickRecognizerRef recognizer,
                                       void *context) {
    start_mode(false);
}

static void select_single_click_handler (ClickRecognizerRef recognizer,
                                         void *context) {
    s_countdown_paused = !s_countdown_paused;
    main_window_refresh();
}

static void select_multi_click_handler (ClickRecognizerRef recognizer,
                                        void *context) {
    window_stack_push(s_menu_window, true);
}

static void click_config_provider (Window *window) {
    window_single_click_subscribe(
        BUTTON_ID_SELECT,
        select_single_click_handler
    );
    window_single_click_subscribe(
        BUTTON_ID_UP,
        up_single_click_handler
    );
    window_single_click_subscribe(
        BUTTON_ID_DOWN,
        down_single_click_handler
    );
    window_multi_click_subscribe(
        BUTTON_ID_SELECT,
        2,
        10,
        0,
        true,
        select_multi_click_handler
    );
    window_long_click_subscribe(
        BUTTON_ID_SELECT,
        0,
        select_multi_click_handler,
        NULL
    );
}

static void init() {
    wakeup_cancel_all();

    // Initialize core.
    init_settings();
    build_menu();

    // Setup clock updates.
    tick_timer_service_subscribe(SECOND_UNIT, time_tick_handler);

    // Create and configure main window.
    s_main_window = window_create();

    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .appear = main_window_refresh,
        .unload = main_window_unload
    });

    window_set_click_config_provider(
        s_main_window,
        (ClickConfigProvider) click_config_provider
    );

    // Create and configure menu window.
    s_menu_window = window_create();

    window_set_window_handlers(s_menu_window, (WindowHandlers) {
        .load = menu_window_load,
        .unload = menu_window_unload
    });

    // Kick things off.
    window_stack_push(s_main_window, true);
}

static void deinit() {
    persist_status();
    if (!s_countdown_paused) {
        int next_vibra =
            (s_countdown_seconds > 0) ? s_countdown_seconds : 60 + s_countdown_seconds % 60;
        time_t wakeup_time = time(NULL) - 15 + ((next_vibra > 18) ? next_vibra : next_vibra + 60);
        wakeup_schedule(wakeup_time, 0, false);
    }
    window_destroy(s_main_window);
    window_destroy(s_menu_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
