// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Drawing of the screens. Top screen: header, details of the selected entity
// and button legend. Bottom screen: touchable grid of entity tiles.

#include <stdio.h>
#include <string.h>

#include "app.h"

// Palette (dark theme, inspired by the Home Assistant dark mode)
#define C_BG        C_RGB(17, 24, 39)
#define C_PANEL     C_RGB(31, 41, 55)
#define C_PANEL_HI  C_RGB(55, 65, 81)
#define C_TEXT      C_RGB(243, 244, 246)
#define C_MUTED     C_RGB(156, 163, 175)
#define C_DARK_TEXT C_RGB(17, 24, 39)
#define C_ACCENT    C_RGB(3, 169, 244)
#define C_AMBER     C_RGB(253, 186, 48)
#define C_ORANGE    C_RGB(249, 115, 22)
#define C_GREEN     C_RGB(52, 199, 89)
#define C_PURPLE    C_RGB(167, 139, 250)
#define C_RED       C_RGB(239, 68, 68)
#define C_RED_DARK  C_RGB(69, 26, 26)
#define C_WHITE     C_RGB(255, 255, 255)

static color_t domain_color(entity_domain d)
{
    switch (d)
    {
        case DOM_LIGHT:
            return C_AMBER;
        case DOM_CLIMATE:
        case DOM_HUMIDIFIER:
            return C_ORANGE;
        case DOM_COVER:
        case DOM_LOCK:
        case DOM_VACUUM:
            return C_GREEN;
        case DOM_MEDIA_PLAYER:
        case DOM_SCENE:
        case DOM_SCRIPT:
        case DOM_BUTTON:
        case DOM_INPUT_BUTTON:
            return C_PURPLE;
        default:
            return C_ACCENT;
    }
}

static bool is_toggleable(entity_domain d)
{
    switch (d)
    {
        case DOM_SENSOR:
        case DOM_BINARY_SENSOR:
        case DOM_WEATHER:
        case DOM_PERSON:
        case DOM_NUMBER:
        case DOM_INPUT_NUMBER:
        case DOM_SCENE:
        case DOM_SCRIPT:
        case DOM_BUTTON:
        case DOM_INPUT_BUTTON:
        case DOM_OTHER:
            return false;
        default:
            return true;
    }
}

// Small icons drawn with rectangles (8x8 cells)
static void draw_house(color_t *fb, int x, int y, color_t c)
{
    // Roof
    for (int i = 0; i < 6; i++)
        gfx_hline(fb, x + 6 - i, y + i, 2 + i * 2, c);
    // Walls
    gfx_rect(fb, x + 2, y + 6, 10, 7, c);
    // Door
    gfx_rect(fb, x + 6, y + 9, 2, 4, C_BG);
}

static void draw_wifi(color_t *fb, int x, int y, int bars, color_t on,
                      color_t off)
{
    for (int i = 0; i < 3; i++)
    {
        int h = 3 + i * 3;
        gfx_rect(fb, x + i * 4, y + 9 - h, 3, h, i < bars ? on : off);
    }
}

// Header bar of the top screen
static void draw_header(app_t *app, color_t *fb)
{
    gfx_rect(fb, 0, 0, GFX_W, 18, C_PANEL);
    draw_house(fb, 4, 2, C_ACCENT);
    gfx_text(fb, 22, 5,
             app->ha->layout_title[0] ? app->ha->layout_title : app->cfg->title,
             C_TEXT, 1, 160);

    char clock[8];
    snprintf(clock, sizeof(clock), "%02d:%02d", app->hour, app->minute);
    gfx_text(fb, GFX_W - 52, 5, clock, C_TEXT, 1, 0);
    draw_wifi(fb, GFX_W - 15, 4, app->wifi_quality, C_TEXT, C_PANEL_HI);
}

static void draw_legend_item(color_t *fb, int *x, int y, const char *key,
                             const char *label)
{
    if (label == NULL)
        return;
    int kw = gfx_text_width(key, 1) + 4;
    gfx_round_rect(fb, *x, y - 2, kw, 12, C_PANEL_HI);
    gfx_text(fb, *x + 2, y, key, C_TEXT, 1, 0);
    *x += kw + 3;
    *x += gfx_text(fb, *x, y, label, C_MUTED, 1, 0) + 8;
}

static void draw_toast(app_t *app, color_t *fb, int y)
{
    if (app->toast[0] == '\0')
        return;
    color_t bg = app->toast_error ? C_RED_DARK : C_PANEL_HI;
    color_t fg = app->toast_error ? C_RED : C_TEXT;
    gfx_rect(fb, 0, y, GFX_W, 14, bg);
    gfx_text_centered(fb, 2, y + 3, GFX_W - 4, app->toast, fg, 1);
}

// Draws the value of the state as big as possible
static void draw_big_state(color_t *fb, int x, int y, int w, const char *s,
                           color_t c)
{
    int scale = 3;
    while (scale > 1 && gfx_text_width(s, scale) > w)
        scale--;
    gfx_text(fb, x, y + (3 - scale) * 4, s, c, scale, w);
}

static void detail_line(color_t *fb, int *y, const char *label,
                        const char *value)
{
    gfx_text(fb, 10, *y, label, C_MUTED, 1, 0);
    gfx_text(fb, 10 + 12 * GFX_CHAR_W, *y, value, C_TEXT, 1,
             GFX_W - 20 - 12 * GFX_CHAR_W);
    *y += 11;
}

static void fmt_num(char *out, size_t len, float v, const char *suffix)
{
    snprintf(out, len, "%.1f", (double)v);
    size_t l = strlen(out);
    if (l > 2 && out[l - 1] == '0' && out[l - 2] == '.')
        out[l - 2] = '\0';
    strncat(out, suffix, len - strlen(out) - 1);
}

static void draw_entity_details(app_t *app, color_t *fb)
{
    if (app->list_count == 0)
    {
        gfx_text_centered(fb, 0, 70, GFX_W, "No entities", C_TEXT, 2);
        gfx_text_wrap(fb, 16, 100, GFX_W - 32, 4, 11,
                      app->ha->auto_mode
                          ? "No supported entities were found. Check the "
                            "'domains' setting in config.ini."
                          : "Add 'entity=' lines to config.ini.",
                      C_MUTED, 1);
        return;
    }

    ha_entity *e = app_entity_at(app, app->selected);
    if (e == NULL)
    {
        gfx_text_centered(fb, 0, 60, GFX_W, "Empty slot", C_MUTED, 2);
        gfx_text_wrap(fb, 24, 90, GFX_W - 48, 4, 11,
                      "Put something here with the DS Dashboard panel in "
                      "Home Assistant.", C_MUTED, 1);
        return;
    }
    color_t dc = domain_color(e->domain);
    char state[64], buf[64];

    entity_format_state(e, state, sizeof(state));

    // Card
    gfx_rect(fb, 4, 24, GFX_W - 8, 116, C_PANEL);
    gfx_rect(fb, 4, 24, 3, 116, entity_is_on(e) ? dc : C_PANEL_HI);

    gfx_text(fb, 12, 29, entity_domain_label(e->domain), dc, 1, 0);
    if (e->pending)
        gfx_text(fb, GFX_W - 12 - 10 * GFX_CHAR_W, 29, "Updating..",
                 C_MUTED, 1, 0);

    // Friendly name, up to two lines at scale 2 if it fits, else scale 1
    int name_lines;
    if (gfx_text_width(e->name, 2) <= (GFX_W - 24) * 2)
        name_lines = gfx_text_wrap(fb, 12, 41, GFX_W - 24, 2, 17, e->name,
                                   C_TEXT, 2);
    else
        name_lines = gfx_text_wrap(fb, 12, 41, GFX_W - 24, 3, 10, e->name,
                                   C_TEXT, 1) / 2 + 1;

    int y = 41 + name_lines * 17 + 4;
    color_t sc = entity_is_unavailable(e) ? C_RED
                 : entity_is_on(e)        ? dc
                                          : C_TEXT;
    draw_big_state(fb, 12, y, GFX_W - 24, state, sc);
    y += 28;

    // Extra details depending on the domain
    switch (e->domain)
    {
        case DOM_CLIMATE:
            if (e->target_temperature != ATTR_NONE)
            {
                fmt_num(buf, sizeof(buf), e->target_temperature, "\xC2\xB0");
                detail_line(fb, &y, "Target", buf);
            }
            if (e->extra[0])
                detail_line(fb, &y, "Action", e->extra);
            break;
        case DOM_MEDIA_PLAYER:
            if (e->extra[0])
                detail_line(fb, &y, "Playing", e->extra);
            if (e->volume != ATTR_NONE)
            {
                snprintf(buf, sizeof(buf), "%d%%", (int)(e->volume * 100 + 0.5f));
                detail_line(fb, &y, "Volume", buf);
            }
            break;
        case DOM_NUMBER:
        case DOM_INPUT_NUMBER:
            if (e->min_value != ATTR_NONE && e->max_value != ATTR_NONE)
            {
                char a[16], b[16];
                fmt_num(a, sizeof(a), e->min_value, "");
                fmt_num(b, sizeof(b), e->max_value, "");
                snprintf(buf, sizeof(buf), "%s - %s", a, b);
                detail_line(fb, &y, "Range", buf);
            }
            break;
        default:
            if (e->extra[0])
                detail_line(fb, &y, "Info", e->extra);
            break;
    }

    if (y <= 128)
        gfx_text(fb, 12, 129, e->id, C_MUTED, 1, GFX_W - 24);
}

static void draw_info_panel(app_t *app, color_t *fb)
{
    char buf[160];
    int y = 26;

    gfx_rect(fb, 4, 22, GFX_W - 8, 140, C_PANEL);
    gfx_text(fb, 10, y, "DSHA " DSHA_VERSION, C_ACCENT, 1, 0);
    gfx_text(fb, GFX_W - 10 - 6 * GFX_CHAR_W, y, "B: back", C_MUTED, 1, 0);
    y += 14;

    if (app->ha->demo)
        snprintf(buf, sizeof(buf), "none (demo mode)");
    else
        snprintf(buf, sizeof(buf), "%s:%d", app->cfg->host, app->cfg->port);
    detail_line(fb, &y, "Server", buf);
    detail_line(fb, &y, "DS IP", app->ip);
    snprintf(buf, sizeof(buf), "%d%s (%s)", app->ha->count,
             app->ha->skipped ? "+" : "",
             app->ha->layout_mode ? "DS Dashboard"
             : app->ha->auto_mode ? "auto" : "config");
    detail_line(fb, &y, "Entities", buf);
    snprintf(buf, sizeof(buf), "%lu req, %lu KB",
             (unsigned long)app->ha->http.requests,
             (unsigned long)(app->ha->http.bytes_received / 1024));
    detail_line(fb, &y, "Traffic", buf);
    if (app->cfg->refresh_seconds > 0)
        snprintf(buf, sizeof(buf), "every %d s", app->cfg->refresh_seconds);
    else
        snprintf(buf, sizeof(buf), "manual");
    detail_line(fb, &y, "Refresh", buf);

    y += 4;
    gfx_text(fb, 10, y, "D-pad select   A action", C_MUTED, 1, 0);
    y += 10;
    gfx_text(fb, 10, y, "L/R dim, temp, volume", C_MUTED, 1, 0);
    y += 10;
    gfx_text(fb, 10, y, app->ha->layout_mode ? "Y refresh  SELECT next page"
                                             : "Y refresh  SELECT filter",
             C_MUTED, 1, 0);
    y += 10;
    gfx_text(fb, 10, y, "START reload all", C_MUTED, 1, 0);
    y += 12;
    gfx_text(fb, 10, y, "A: sign in / set up again", C_ACCENT, 1, 0);
}

static void draw_top_dashboard(app_t *app, color_t *fb)
{
    gfx_clear(fb, C_BG);
    draw_header(app, fb);

    if (app->info_visible)
        draw_info_panel(app, fb);
    else
        draw_entity_details(app, fb);

    // Button legend
    gfx_rect(fb, 0, 166, GFX_W, 26, C_PANEL);
    int x = 4;
    ha_entity *sel = app_entity_at(app, app->selected);
    if (sel != NULL && !app->info_visible)
    {
        ha_entity *e = sel;
        draw_legend_item(fb, &x, 168, "A", entity_action_label(e, ACT_PRIMARY));
        draw_legend_item(fb, &x, 168, "L",
                         entity_action_label(e, ACT_DECREASE));
        draw_legend_item(fb, &x, 168, "R",
                         entity_action_label(e, ACT_INCREASE));
    }
    x = 4;
    draw_legend_item(fb, &x, 181, "Y", "Refresh");
    draw_legend_item(fb, &x, 181, "X", app->info_visible ? "Entity" : "Info");
    draw_legend_item(fb, &x, 181, "SEL",
                     app->ha->layout_mode ? "Page" : "Filter");

    draw_toast(app, fb, 150);
}

static void draw_tile(app_t *app, color_t *fb, int list_index, int x, int y)
{
    ha_entity *e = app_entity_at(app, list_index);
    bool selected = list_index == app->selected;

    if (e == NULL)
    {
        // Empty slot of the layout
        gfx_frame(fb, x + 2, y + 2, TILE_W - 4, TILE_H - 4, C_PANEL);
        if (selected)
        {
            gfx_frame(fb, x, y, TILE_W, TILE_H, C_WHITE);
            gfx_frame(fb, x + 1, y + 1, TILE_W - 2, TILE_H - 2, C_WHITE);
        }
        return;
    }

    bool on = entity_is_on(e);
    bool unavailable = entity_is_unavailable(e);
    color_t dc = domain_color(e->domain);

    color_t bg, fg, sub;
    if (unavailable)
    {
        bg = C_RED_DARK;
        fg = C_MUTED;
        sub = C_RED;
    }
    else if (on && is_toggleable(e->domain))
    {
        bg = dc;
        fg = C_DARK_TEXT;
        sub = C_DARK_TEXT;
    }
    else
    {
        bg = C_PANEL;
        fg = C_TEXT;
        sub = is_toggleable(e->domain) ? C_MUTED : dc;
    }

    gfx_round_rect(fb, x, y, TILE_W, TILE_H, bg);

    // Domain stripe for tiles that aren't filled with the domain color
    if (bg == C_PANEL)
        gfx_rect(fb, x + 3, y + 4, 2, TILE_H - 8, dc);

    gfx_text_wrap(fb, x + 8, y + 4, TILE_W - 12, 2, 10, e->name, fg, 1);

    char state[48];
    if (e->pending)
        snprintf(state, sizeof(state), "...");
    else if (!e->loaded)
        snprintf(state, sizeof(state), "-");
    else
        entity_format_state(e, state, sizeof(state));
    gfx_text(fb, x + 8, y + TILE_H - 11, state, sub, 1, TILE_W - 12);

    if (selected)
    {
        gfx_frame(fb, x, y, TILE_W, TILE_H, C_WHITE);
        gfx_frame(fb, x + 1, y + 1, TILE_W - 2, TILE_H - 2, C_WHITE);
    }
}

static void draw_arrow(color_t *fb, int cx, int cy, int dir, color_t c)
{
    // Triangle pointing left (dir < 0) or right (dir > 0)
    for (int i = 0; i < 6; i++)
    {
        int h = 1 + i * 2;
        int x = dir < 0 ? cx - 3 + i : cx + 3 - i;
        gfx_rect(fb, x, cy - i, 1, h, c);
    }
}

// Circular arrow: a ring with a gap at the top right and an arrow head
static void draw_refresh_icon(color_t *fb, int cx, int cy, color_t c)
{
    for (int dy = -7; dy <= 7; dy++)
    {
        for (int dx = -7; dx <= 7; dx++)
        {
            int d2 = dx * dx + dy * dy;
            if (d2 < 25 || d2 > 49)
                continue;
            if (dx > 0 && dy < 0 && dx < 6) // Gap
                continue;
            gfx_rect(fb, cx + dx, cy + dy, 1, 1, c);
        }
    }
    // Arrow head pointing up at the end of the ring (right side)
    for (int i = 0; i < 4; i++)
        gfx_hline(fb, cx + 6 - i, cy - 3 - i, 1 + i * 2, c);
}

static void draw_bottom_dashboard(app_t *app, color_t *fb)
{
    gfx_clear(fb, C_BG);

    int page = app->selected / TILES_PER_PAGE;
    int pages = (app->list_count + TILES_PER_PAGE - 1) / TILES_PER_PAGE;
    if (pages < 1)
        pages = 1;

    for (int i = 0; i < TILES_PER_PAGE; i++)
    {
        int li = page * TILES_PER_PAGE + i;
        if (li >= app->list_count)
            break;
        int col = i % TILE_COLS;
        int row = i / TILE_COLS;
        draw_tile(app, fb, li, TILE_X0 + col * (TILE_W + TILE_GAP),
                  TILE_Y0 + row * (TILE_H + TILE_GAP));
    }

    // Footer: [<] [filter + page] [refresh] [>]
    int y = FOOTER_Y + 2;
    int h = GFX_H - y - 2;
    gfx_round_rect(fb, 2, y, 44, h, C_PANEL_HI);
    draw_arrow(fb, 24, y + h / 2, -1, C_TEXT);

    gfx_round_rect(fb, 48, y, 110, h, C_PANEL);
    gfx_text_centered(fb, 50, y + 4, 106, app_page_title(app),
                      C_TEXT, 1);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d/%d", page + 1, pages);
    gfx_text_centered(fb, 50, y + 14, 106, buf, C_MUTED, 1);

    gfx_round_rect(fb, 160, y, 46, h, C_PANEL);
    draw_refresh_icon(fb, 183, y + h / 2, C_TEXT);

    gfx_round_rect(fb, 208, y, 46, h, C_PANEL_HI);
    draw_arrow(fb, 231, y + h / 2, 1, C_TEXT);
}

static void draw_message(app_t *app, color_t *top, color_t *bottom)
{
    // Top screen: title and text
    gfx_clear(top, C_BG);
    gfx_rect(top, 0, 0, GFX_W, 18, C_PANEL);
    draw_house(top, 4, 2, C_ACCENT);
    gfx_text(top, 22, 5, "DSHA - Home Assistant for DS", C_TEXT, 1, 0);

    color_t tc = app->msg_error ? C_RED : C_ACCENT;
    gfx_text_wrap(top, 12, 30, GFX_W - 24, 2, 18, app->msg_title, tc, 2);
    gfx_text_wrap(top, 12, 70, GFX_W - 24, 10, 11, app->msg_body, C_TEXT, 1);

    // Bottom screen: logo, animation and hint
    gfx_clear(bottom, C_BG);

    // Big house logo
    int lx = 128 - 28, ly = 40;
    for (int i = 0; i < 24; i++)
        gfx_hline(bottom, lx + 28 - i - 4, ly + i, 8 + i * 2, C_ACCENT);
    gfx_rect(bottom, lx + 6, ly + 24, 44, 26, C_ACCENT);
    gfx_rect(bottom, lx + 22, ly + 34, 12, 16, C_BG);
    gfx_text_centered(bottom, 0, 100, GFX_W, "DSHA", C_TEXT, 3);
    gfx_text_centered(bottom, 0, 128, GFX_W, "v" DSHA_VERSION, C_MUTED, 1);

    if (app->msg_busy)
    {
        int phase = (int)((app->frame / 15) % 4);
        for (int i = 0; i < 3; i++)
            gfx_rect(bottom, 116 + i * 10, 146, 6, 6,
                     i < phase ? C_ACCENT : C_PANEL_HI);
    }

    if (app->msg_hint[0])
        gfx_text_wrap(bottom, 8, 166, GFX_W - 16, 2, 11, app->msg_hint,
                      C_MUTED, 1);
}

void app_draw(app_t *app, color_t *top, color_t *bottom)
{
    if (app->screen == SCREEN_MESSAGE)
    {
        draw_message(app, top, bottom);
    }
    else if (app->screen == SCREEN_KEYBOARD)
    {
        kbd_draw(&app->kbd, top, bottom);
    }
    else if (app->screen == SCREEN_PICKER)
    {
        picker_draw(&app->picker, top, bottom);
    }
    else
    {
        draw_top_dashboard(app, top);
        draw_bottom_dashboard(app, bottom);
    }
    app->dirty = false;
}
