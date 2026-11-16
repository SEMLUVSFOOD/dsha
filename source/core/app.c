// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <stdio.h>
#include <string.h>

#include "app.h"

#define LONG_PRESS_MS 450
#define TOAST_MS 3000
#define RECHECK_DELAY_MS 1500

static const char *const filter_names[FILTER_COUNT] = {
    [FILTER_ALL] = "All",
    [FILTER_LIGHTS] = "Lights",
    [FILTER_SWITCHES] = "Switches",
    [FILTER_CLIMATE] = "Climate",
    [FILTER_COVERS] = "Covers & locks",
    [FILTER_MEDIA] = "Media",
    [FILTER_SCENES] = "Scenes & scripts",
    [FILTER_SENSORS] = "Sensors",
};

const char *app_filter_name(app_filter f)
{
    return filter_names[f];
}

static bool filter_match(app_filter f, entity_domain d)
{
    switch (f)
    {
        case FILTER_ALL:
            return true;
        case FILTER_LIGHTS:
            return d == DOM_LIGHT;
        case FILTER_SWITCHES:
            return d == DOM_SWITCH || d == DOM_INPUT_BOOLEAN || d == DOM_FAN
                   || d == DOM_AUTOMATION || d == DOM_SIREN
                   || d == DOM_HUMIDIFIER;
        case FILTER_CLIMATE:
            return d == DOM_CLIMATE;
        case FILTER_COVERS:
            return d == DOM_COVER || d == DOM_LOCK || d == DOM_VACUUM;
        case FILTER_MEDIA:
            return d == DOM_MEDIA_PLAYER;
        case FILTER_SCENES:
            return d == DOM_SCENE || d == DOM_SCRIPT || d == DOM_BUTTON
                   || d == DOM_INPUT_BUTTON;
        case FILTER_SENSORS:
            return d == DOM_SENSOR || d == DOM_BINARY_SENSOR
                   || d == DOM_WEATHER || d == DOM_PERSON || d == DOM_NUMBER
                   || d == DOM_INPUT_NUMBER || d == DOM_OTHER;
        default:
            return false;
    }
}

static int count_filter(const app_t *app, app_filter f)
{
    int n = 0;
    for (int i = 0; i < app->ha->count; i++)
    {
        if (filter_match(f, app->ha->entities[i].domain))
            n++;
    }
    return n;
}

const char *app_page_title(const app_t *app)
{
    if (app->ha->layout_mode)
    {
        int page = app->selected / TILES_PER_PAGE;
        if (page < app->ha->num_pages)
            return app->ha->pages[page].name;
    }
    return filter_names[app->filter];
}

ha_entity *app_entity_at(const app_t *app, int list_index)
{
    if (list_index < 0 || list_index >= app->list_count)
        return NULL;
    int idx = app->list[list_index];
    return idx >= 0 ? &app->ha->entities[idx] : NULL;
}

static void build_list(app_t *app)
{
    app->list_count = 0;

    if (app->ha->layout_mode)
    {
        // The pages of the DS Dashboard panel, empty slots included
        for (int p = 0; p < app->ha->num_pages; p++)
        {
            for (int i = 0; i < TILES_PER_PAGE && i < LAYOUT_SLOTS; i++)
                app->list[app->list_count++] = app->ha->pages[p].slots[i];
        }
    }
    else for (int i = 0; i < app->ha->count; i++)
    {
        if (filter_match(app->filter, app->ha->entities[i].domain))
            app->list[app->list_count++] = i;
    }
    if (app->selected >= app->list_count)
        app->selected = app->list_count > 0 ? app->list_count - 1 : 0;
    if (app->selected < 0)
        app->selected = 0;
}

void app_init(app_t *app, ha_config *cfg, ha_client *ha)
{
    memset(app, 0, sizeof(*app));
    app->cfg = cfg;
    app->ha = ha;
    app->queued_entity = -1;
    app->recheck_entity = -1;
    app->dirty = true;
    snprintf(app->ip, sizeof(app->ip), "-");
}

void app_show_message(app_t *app, const char *title, const char *body,
                      const char *hint, bool error, bool busy)
{
    app->screen = SCREEN_MESSAGE;
    snprintf(app->msg_title, sizeof(app->msg_title), "%s", title);
    snprintf(app->msg_body, sizeof(app->msg_body), "%s", body ? body : "");
    snprintf(app->msg_hint, sizeof(app->msg_hint), "%s", hint ? hint : "");
    app->msg_error = error;
    app->msg_busy = busy;
    app->dirty = true;
}

void app_show_dashboard(app_t *app)
{
    app->screen = SCREEN_DASHBOARD;
    if (app->filter != FILTER_ALL && count_filter(app, app->filter) == 0)
        app->filter = FILTER_ALL;
    build_list(app);
    app->last_refresh_ms = app->now_ms;
    app->refresh_count = 0;
    app->page_refresh_pending = false;
    app->queued_entity = -1;
    app->recheck_entity = -1;
    app->dirty = true;
}

void app_show_keyboard(app_t *app)
{
    app->screen = SCREEN_KEYBOARD;
    app->dirty = true;
}

void app_show_picker(app_t *app)
{
    app->screen = SCREEN_PICKER;
    app->dirty = true;
}

void app_toast(app_t *app, const char *text, bool error)
{
    snprintf(app->toast, sizeof(app->toast), "%s", text);
    app->toast_error = error;
    app->toast_until_ms = app->now_ms + TOAST_MS;
    app->dirty = true;
}

static void queue_action(app_t *app, int list_index, entity_action a)
{
    if (list_index < 0 || list_index >= app->list_count)
        return;
    int idx = app->list[list_index];
    if (idx < 0)
        return; // Empty slot
    ha_entity *e = &app->ha->entities[idx];

    if (!entity_has_action(e, a))
    {
        if (a != ACT_PRIMARY || entity_is_unavailable(e))
            app_toast(app, entity_is_unavailable(e) ? "Entity is unavailable"
                                                    : "Not available here",
                      false);
        return;
    }

    e->pending = true;
    app->queued_entity = idx;
    app->queued_action = a;
    app->dirty = true;
}

static void change_page(app_t *app, int delta);

static void next_filter(app_t *app)
{
    // With a layout, the button goes to the next page instead
    if (app->ha->layout_mode)
    {
        change_page(app, 1);
        return;
    }

    // Skip groups without entities
    for (int i = 1; i <= FILTER_COUNT; i++)
    {
        app_filter f = (app_filter)((app->filter + i) % FILTER_COUNT);
        if (f == FILTER_ALL || count_filter(app, f) > 0)
        {
            app->filter = f;
            break;
        }
    }
    app->selected = 0;
    build_list(app);
    app->refresh_count = 0;
    app->last_refresh_ms = 0; // Refresh the new page soon
    app->dirty = true;
}

static int page_count(const app_t *app)
{
    int n = (app->list_count + TILES_PER_PAGE - 1) / TILES_PER_PAGE;
    return n < 1 ? 1 : n;
}

static void change_page(app_t *app, int delta)
{
    int pages = page_count(app);
    int page = app->selected / TILES_PER_PAGE;
    int pos = app->selected % TILES_PER_PAGE;
    page = (page + delta + pages) % pages;
    app->selected = page * TILES_PER_PAGE + pos;
    if (app->selected >= app->list_count)
        app->selected = app->list_count > 0 ? app->list_count - 1 : 0;
    app->refresh_count = 0;
    app->last_refresh_ms = 0; // Refresh the new page soon
    app->dirty = true;
}

static void move_selection(app_t *app, int dx, int dy)
{
    if (app->list_count == 0)
        return;

    int page = app->selected / TILES_PER_PAGE;
    int pos = app->selected % TILES_PER_PAGE;
    int col = pos % TILE_COLS;
    int row = pos / TILE_COLS;

    if (dx != 0)
    {
        col += dx;
        if (col < 0 || col >= TILE_COLS)
        {
            // Going past the edge changes page, like a carousel
            change_page(app, dx);
            page = app->selected / TILES_PER_PAGE;
            col = dx < 0 ? TILE_COLS - 1 : 0;
        }
    }
    if (dy != 0)
    {
        int idx = app->selected + dy * TILE_COLS;
        if (idx >= 0 && idx < app->list_count)
        {
            app->selected = idx;
            if (app->selected / TILES_PER_PAGE != page)
            {
                app->refresh_count = 0;
                app->last_refresh_ms = 0;
            }
            app->dirty = true;
        }
        return;
    }

    int idx = page * TILES_PER_PAGE + row * TILE_COLS + col;
    if (idx >= app->list_count)
        idx = app->list_count - 1;
    app->selected = idx;
    app->dirty = true;
}

// Returns the list index of the tile at (x, y), or -1
static int tile_at(const app_t *app, int x, int y)
{
    if (y >= FOOTER_Y)
        return -1;
    int col = (x - TILE_X0) / (TILE_W + TILE_GAP);
    int row = (y - TILE_Y0) / (TILE_H + TILE_GAP);
    if (col < 0 || col >= TILE_COLS || row < 0 || row >= TILE_ROWS)
        return -1;
    int page = app->selected / TILES_PER_PAGE;
    int idx = page * TILES_PER_PAGE + row * TILE_COLS + col;
    return idx < app->list_count ? idx : -1;
}

static void start_page_refresh(app_t *app);

static void handle_touch(app_t *app, const app_input *in)
{
    // If the release of a previous touch was missed (for example, while a
    // network request was blocking), treat this as a new touch.
    if (in->touching && app->touch_active
        && (int32_t)(app->now_ms - app->touch_last_ms) > 250)
        app->touch_active = false;

    if (in->touching)
        app->touch_last_ms = app->now_ms;

    if (in->touching && app->touch_active)
    {
        // Follow the finger: the tile under it when it's released is used
        app->touch_x = in->tx;
        app->touch_y = in->ty;
        return;
    }

    if (in->touching && !app->touch_active)
    {
        app->touch_active = true;
        app->touch_consumed = false;
        app->touch_x = in->tx;
        app->touch_y = in->ty;
        app->touch_start_ms = app->now_ms;

        // Footer buttons react immediately
        if (in->ty >= FOOTER_Y)
        {
            if (in->tx < 48)
                change_page(app, -1);
            else if (in->tx >= 208)
                change_page(app, 1);
            else if (in->tx >= 160)
            {
                start_page_refresh(app);
                app_toast(app, "Refreshing...", false);
            }
            else
                next_filter(app);
            app->touch_consumed = true; // Don't treat it as a tile tap
        }
        return;
    }

    if (!in->touching && app->touch_active)
    {
        app->touch_active = false;
        if (app->touch_consumed)
            return;
        int idx = tile_at(app, app->touch_x, app->touch_y);
        if (idx < 0)
            return;

        app->selected = idx;
        app->dirty = true;

        // Short tap: run the main action. Long press: only select.
        if ((int32_t)(app->now_ms - app->touch_start_ms) < LONG_PRESS_MS)
        {
            ha_entity *e = app_entity_at(app, idx);
            if (e != NULL && entity_has_action(e, ACT_PRIMARY))
                queue_action(app, idx, ACT_PRIMARY);
        }
    }
}

static void start_page_refresh(app_t *app)
{
    int page = app->selected / TILES_PER_PAGE;
    app->refresh_count = 0;

    if (app->ha->layout_mode)
    {
        app->page_refresh_pending = true;
        app->page_refresh_page = page;
        app->last_refresh_ms = app->now_ms;
        return;
    }

    for (int i = 0; i < TILES_PER_PAGE; i++)
    {
        int li = page * TILES_PER_PAGE + i;
        if (li >= app->list_count)
            break;
        if (app->list[li] >= 0)
            app->refresh_queue[app->refresh_count++] = app->list[li];
    }
    app->last_refresh_ms = app->now_ms;
}

// Does at most one network request per frame so the UI stays responsive
static void network_step(app_t *app)
{
    ha_client *ha = app->ha;

    if (app->queued_entity >= 0)
    {
        ha_entity *e = &ha->entities[app->queued_entity];
        entity_action a = app->queued_action;
        app->queued_entity = -1;

        if (ha_action(ha, e, a))
        {
            // Check again in a moment: many devices take a while to report
            // their new state.
            app->recheck_entity = (int)(e - ha->entities);
            app->recheck_at_ms = app->now_ms + RECHECK_DELAY_MS;
        }
        else
        {
            e->pending = false;
            app_toast(app, ha->error, true);
        }
        app->dirty = true;
        return;
    }

    if (app->recheck_entity >= 0
        && (int32_t)(app->now_ms - app->recheck_at_ms) >= 0)
    {
        ha_entity *e = &ha->entities[app->recheck_entity];
        app->recheck_entity = -1;
        if (!ha_refresh(ha, e))
            app_toast(app, ha->error, true);
        e->pending = false;
        app->dirty = true;
        return;
    }

    if (app->page_refresh_pending)
    {
        app->page_refresh_pending = false;
        if (!ha_refresh_page(ha, app->page_refresh_page))
            app_toast(app, ha->error, true);
        app->dirty = true;
        return;
    }

    if (app->refresh_count > 0)
    {
        int idx = app->refresh_queue[--app->refresh_count];
        ha_entity *e = &ha->entities[idx];
        if (!ha_refresh(ha, e) && ha->last_status != 404)
        {
            app_toast(app, ha->error, true);
            app->refresh_count = 0; // Don't spam errors
        }
        app->dirty = true;
        return;
    }

    bool auto_refresh = app->cfg->refresh_seconds > 0
                        && (int32_t)(app->now_ms - app->last_refresh_ms)
                               >= app->cfg->refresh_seconds * 1000;
    if (app->last_refresh_ms == 0 || auto_refresh)
        start_page_refresh(app);
}

bool app_update(app_t *app, const app_input *in, uint32_t now_ms)
{
    app->now_ms = now_ms;
    app->frame++;

    if (app->msg_busy && (app->frame % 15) == 0)
        app->dirty = true; // Animation

    if (app->toast[0] != '\0' && (int32_t)(now_ms - app->toast_until_ms) > 0)
    {
        app->toast[0] = '\0';
        app->dirty = true;
    }

    if (app->screen == SCREEN_KEYBOARD)
    {
        if (in->down || in->touching || app->kbd.touching)
            app->dirty = true;
        kbd_update(&app->kbd, in);
        return app->dirty;
    }
    if (app->screen == SCREEN_PICKER)
    {
        if (in->down || in->touching != app->picker.touching)
            app->dirty = true;
        picker_update(&app->picker, in);
        return app->dirty;
    }

    if (app->screen != SCREEN_DASHBOARD)
        return app->dirty;

    uint32_t k = in->down;

    if (k & IN_START)
        app->request_reload = true;
    if (k & IN_SELECT)
        next_filter(app);
    if (k & IN_X)
    {
        app->info_visible = !app->info_visible;
        app->dirty = true;
    }
    if ((k & IN_B) && app->info_visible)
    {
        app->info_visible = false;
        app->dirty = true;
    }
    if ((k & IN_A) && app->info_visible)
    {
        // "A: run setup again" in the info panel
        app->request_setup = true;
        return app->dirty;
    }
    if (k & IN_Y)
    {
        start_page_refresh(app);
        app_toast(app, "Refreshing...", false);
    }

    if (k & IN_LEFT)
        move_selection(app, -1, 0);
    if (k & IN_RIGHT)
        move_selection(app, 1, 0);
    if (k & IN_UP)
        move_selection(app, 0, -1);
    if (k & IN_DOWN)
        move_selection(app, 0, 1);

    if (k & IN_A)
        queue_action(app, app->selected, ACT_PRIMARY);
    if (k & IN_L)
        queue_action(app, app->selected, ACT_DECREASE);
    if (k & IN_R)
        queue_action(app, app->selected, ACT_INCREASE);

    handle_touch(app, in);

    // Let the pending state be drawn before doing the request
    if (!app->dirty)
        network_step(app);

    return app->dirty;
}
