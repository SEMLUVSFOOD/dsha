// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Platform independent application logic: input handling, screens, periodic
// refreshes... The platform layer (main.c on the DS) feeds input and copies
// the rendered framebuffers to the screens.

#ifndef DSHA_APP_H
#define DSHA_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "gfx.h"
#include "ha.h"
#include "widgets.h"

#define DSHA_VERSION "1.3.0"

// Logical buttons (the platform maps hardware keys to these)
enum {
    IN_A = 1 << 0,
    IN_B = 1 << 1,
    IN_X = 1 << 2,
    IN_Y = 1 << 3,
    IN_L = 1 << 4,
    IN_R = 1 << 5,
    IN_START = 1 << 6,
    IN_SELECT = 1 << 7,
    IN_UP = 1 << 8,
    IN_DOWN = 1 << 9,
    IN_LEFT = 1 << 10,
    IN_RIGHT = 1 << 11,
};

typedef struct {
    uint32_t down; // Pressed this frame
    uint32_t held; // Currently held
    bool touching;
    int tx, ty;    // Touch position (bottom screen)
} app_input;

typedef enum {
    SCREEN_MESSAGE,   // Full screen message (errors, loading...)
    SCREEN_DASHBOARD,
    SCREEN_KEYBOARD,  // Setup: on-screen keyboard (app->kbd)
    SCREEN_PICKER,    // Setup: list of choices (app->picker)
} app_screen;

typedef enum {
    FILTER_ALL,
    FILTER_LIGHTS,
    FILTER_SWITCHES,
    FILTER_CLIMATE,
    FILTER_COVERS,
    FILTER_MEDIA,
    FILTER_SCENES,
    FILTER_SENSORS,
    FILTER_COUNT
} app_filter;

// Layout of the bottom screen
#define TILE_COLS 2
#define TILE_ROWS 4
#define TILES_PER_PAGE (TILE_COLS * TILE_ROWS)
#define TILE_W 125
#define TILE_H 38
#define TILE_GAP 2
#define TILE_X0 2
#define TILE_Y0 2
#define FOOTER_Y 164

typedef struct {
    ha_config *cfg;
    ha_client *ha;

    app_screen screen;
    uint32_t frame;
    uint32_t now_ms;

    // Message screen
    char msg_title[48];
    char msg_body[512];
    char msg_hint[96];
    bool msg_error;
    bool msg_busy; // Shows an animation

    // Dashboard
    app_filter filter;
    int list[ENTITY_MAX]; // Indices of the entities that pass the filter
    int list_count;
    int selected;         // Index into list[]
    bool info_visible;    // Info/help panel on the top screen

    // Actions are queued so that the "pending" state is drawn before the
    // (blocking) network request is done.
    int queued_entity;    // -1 if none
    entity_action queued_action;

    // Periodic refresh of the visible page
    uint32_t last_refresh_ms;
    int refresh_queue[TILES_PER_PAGE];
    int refresh_count;
    bool page_refresh_pending; // Layout mode: one request per page
    int page_refresh_page;

    // Entity to re-check shortly after an action
    int recheck_entity;
    uint32_t recheck_at_ms;

    // Touch handling
    bool touch_active;
    int touch_x, touch_y;
    uint32_t touch_start_ms;
    uint32_t touch_last_ms; // Last frame in which the screen was touched
    bool touch_consumed;    // Touch used by a footer button: not a tile tap

    // Status messages on the top screen
    char toast[96];
    bool toast_error;
    uint32_t toast_until_ms;

    // Network info (filled by the platform layer)
    char ip[32];
    int wifi_quality; // 0-3 bars

    // Clock (filled by the platform layer)
    int hour, minute;

    // Setup widgets
    keyboard kbd;
    picker picker;

    bool request_reload; // The user wants a full reload (START)
    bool request_setup;  // The user wants to run the setup again
    bool dirty;
} app_t;

void app_init(app_t *app, ha_config *cfg, ha_client *ha);

void app_show_message(app_t *app, const char *title, const char *body,
                      const char *hint, bool error, bool busy);

// Switch to the dashboard after the entities have been loaded
void app_show_dashboard(app_t *app);

// Run one frame of the application. Returns true if the screens need to be
// redrawn.
bool app_update(app_t *app, const app_input *in, uint32_t now_ms);

// Render both screens
void app_draw(app_t *app, color_t *top, color_t *bottom);

void app_toast(app_t *app, const char *text, bool error);

// Show the keyboard / picker (initialize app->kbd / app->picker first)
void app_show_keyboard(app_t *app);
void app_show_picker(app_t *app);

const char *app_filter_name(app_filter f);

// Text of the middle footer button: the filter, or the page name in layout
// mode. Also tells if the list index holds an entity (layout mode can have
// empty slots).
const char *app_page_title(const app_t *app);
ha_entity *app_entity_at(const app_t *app, int list_index);

#endif // DSHA_APP_H
