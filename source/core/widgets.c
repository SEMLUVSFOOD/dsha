// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "widgets.h"

#define C_BG        C_RGB(17, 24, 39)
#define C_PANEL     C_RGB(31, 41, 55)
#define C_PANEL_HI  C_RGB(55, 65, 81)
#define C_KEY       C_RGB(55, 65, 81)
#define C_KEY_SPEC  C_RGB(75, 85, 99)
#define C_TEXT      C_RGB(243, 244, 246)
#define C_MUTED     C_RGB(156, 163, 175)
#define C_ACCENT    C_RGB(3, 169, 244)
#define C_WHITE     C_RGB(255, 255, 255)

// Keyboard layout
// ---------------

enum {
    KEY_SHIFT = 40,
    KEY_SYM,
    KEY_SPACE,
    KEY_BACK,
    KEY_OK,
    KEY_COUNT
};

// Each key is a UTF-8 string
static const char *const text_layers[3][KBD_ROWS][KBD_COLS] = {
    {
        { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
        { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
        { "a", "s", "d", "f", "g", "h", "j", "k", "l", "-" },
        { "z", "x", "c", "v", "b", "n", "m", ".", "_", "@" },
    },
    {
        { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
        { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" },
        { "A", "S", "D", "F", "G", "H", "J", "K", "L", "-" },
        { "Z", "X", "C", "V", "B", "N", "M", ".", "_", "@" },
    },
    {
        { "!", "@", "#", "$", "%", "^", "&", "*", "(", ")" },
        { "-", "_", "=", "+", "[", "]", "{", "}", "\\", "|" },
        { ";", ":", "'", "\"", ",", "<", ">", "/", "?", "~" },
        { "`", "\xC2\xA3", "\xE2\x82\xAC", "\xC2\xB0", "\xC2\xA7",
          "\xC3\xA9", "\xC3\xA8", "\xC3\xBC", "\xC3\xB6", "\xC3\xB1" },
    },
};

// URL keyboard: the last letters row has ':' and '/' instead of '_' and '@'
static const char *const url_row3[2][KBD_COLS] = {
    { "z", "x", "c", "v", "b", "n", "m", ".", ":", "/" },
    { "Z", "X", "C", "V", "B", "N", "M", ".", ":", "/" },
};

static const char *const numeric_keys[KBD_ROWS][3] = {
    { "1", "2", "3" },
    { "4", "5", "6" },
    { "7", "8", "9" },
    { ".", "0", ":" },
};

typedef struct {
    int x, y, w, h;
} rect;

// Text and URL keyboards share the QWERTY layout
static bool qwerty(const keyboard *k)
{
    return k->mode == KBD_TEXT || k->mode == KBD_URL;
}

static int cols_in_row(const keyboard *k, int row)
{
    if (row == KBD_ROWS)
        return qwerty(k) ? 5 : 2;
    return qwerty(k) ? KBD_COLS : 3;
}

// Key index for a (row, col) of the D-pad cursor
static int key_index(const keyboard *k, int row, int col)
{
    if (row < KBD_ROWS)
        return qwerty(k) ? row * KBD_COLS + col : row * 3 + col;
    if (qwerty(k))
        return KEY_SHIFT + col;
    return col == 0 ? KEY_BACK : KEY_OK;
}

static bool key_rect(const keyboard *k, int key, rect *r)
{
    if (qwerty(k))
    {
        if (key < 40)
        {
            int row = key / KBD_COLS, col = key % KBD_COLS;
            *r = (rect){ 3 + col * 25, 4 + row * 34, 24, 31 };
            return true;
        }
        static const rect special[5] = {
            { 3, 142, 40, 44 },   // Shift
            { 45, 142, 40, 44 },  // Symbols
            { 87, 142, 82, 44 },  // Space
            { 171, 142, 40, 44 }, // Backspace
            { 213, 142, 40, 44 }, // OK
        };
        if (key >= KEY_SHIFT && key < KEY_COUNT)
        {
            *r = special[key - KEY_SHIFT];
            return true;
        }
        return false;
    }

    if (key < 12)
    {
        int row = key / 3, col = key % 3;
        *r = (rect){ 8 + col * 81, 4 + row * 34, 78, 31 };
        return true;
    }
    if (key == KEY_BACK)
    {
        *r = (rect){ 8, 142, 118, 44 };
        return true;
    }
    if (key == KEY_OK)
    {
        *r = (rect){ 130, 142, 118, 44 };
        return true;
    }
    return false;
}

static const char *key_label(const keyboard *k, int key)
{
    if (qwerty(k) && key < 40)
    {
        int layer = k->layer;
        if (layer == 0 && k->shift_once)
            layer = 1;
        if (k->mode == KBD_URL && layer < 2 && key / KBD_COLS == 3)
            return url_row3[layer][key % KBD_COLS];
        return text_layers[layer][key / KBD_COLS][key % KBD_COLS];
    }
    if (k->mode == KBD_NUMERIC && key < 12)
        return numeric_keys[key / 3][key % 3];

    if (k->mode == KBD_URL && key == KEY_SHIFT)
        return "Clear";
    if (k->mode == KBD_URL && key == KEY_SPACE)
        return "https://";

    switch (key)
    {
        case KEY_SHIFT: return k->layer == 1 ? "abc" : "ABC";
        case KEY_SYM: return k->layer == 2 ? "abc" : "#+=";
        case KEY_SPACE: return "space";
        case KEY_BACK: return "\xC2\xAB Del";
        case KEY_OK: return "OK";
        default: return "";
    }
}

void kbd_init(keyboard *k, const char *title, const char *initial,
              int max_len, kbd_mode mode, bool password, bool cancellable)
{
    memset(k, 0, sizeof(*k));
    snprintf(k->title, sizeof(k->title), "%s", title);
    snprintf(k->text, sizeof(k->text), "%s", initial ? initial : "");
    k->max_len = max_len < (int)sizeof(k->text) ? max_len
                                                 : (int)sizeof(k->text) - 1;
    k->mode = mode;
    k->password = password;
    k->cancellable = cancellable;
    k->touch_key = -1;
    k->cur_row = 1;
}

int kbd_key_at(const keyboard *k, int x, int y)
{
    for (int key = 0; key < KEY_COUNT; key++)
    {
        rect r;
        if (!key_rect(k, key, &r))
            continue;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return key;
    }
    return -1;
}

static void backspace(keyboard *k)
{
    size_t len = strlen(k->text);
    if (len == 0)
        return;
    // Remove a whole UTF-8 character
    len--;
    while (len > 0 && ((unsigned char)k->text[len] & 0xC0) == 0x80)
        len--;
    k->text[len] = '\0';
}

// The "https://" key: adds the prefix (replacing "http://"), or removes it
// if it's already there.
static void toggle_https(keyboard *k)
{
    char rest[sizeof(k->text)];
    const char *v = k->text;
    bool had = false;
    if (strncmp(v, "https://", 8) == 0)
    {
        v += 8;
        had = true;
    }
    else if (strncmp(v, "http://", 7) == 0)
        v += 7;
    snprintf(rest, sizeof(rest), "%s", v);
    if (had)
        snprintf(k->text, sizeof(k->text), "%s", rest);
    else if ((int)(strlen(rest) + 8) <= k->max_len)
        snprintf(k->text, sizeof(k->text), "https://%s", rest);
}

static void type_text(keyboard *k, const char *s)
{
    size_t len = strlen(k->text);
    size_t add = strlen(s);
    if ((int)(len + add) > k->max_len)
        return;
    memcpy(k->text + len, s, add + 1);
}

void kbd_press(keyboard *k, int key)
{
    if (key < 0)
        return;

    if (k->mode == KBD_URL && key == KEY_SHIFT)
    {
        k->text[0] = '\0'; // Clear
        return;
    }
    if (k->mode == KBD_URL && key == KEY_SPACE)
    {
        toggle_https(k);
        return;
    }

    switch (key)
    {
        case KEY_SHIFT:
            if (k->layer == 2)
                k->layer = 0;
            else if (k->layer == 1)
                k->layer = 0;
            else if (k->shift_once)
            {
                // Double tap: caps lock
                k->shift_once = false;
                k->layer = 1;
            }
            else
                k->shift_once = true;
            return;
        case KEY_SYM:
            k->layer = k->layer == 2 ? 0 : 2;
            k->shift_once = false;
            return;
        case KEY_SPACE:
            type_text(k, " ");
            return;
        case KEY_BACK:
            backspace(k);
            return;
        case KEY_OK:
            k->done = true;
            return;
        default:
            break;
    }

    const char *label = key_label(k, key);
    if (label[0] != '\0')
    {
        type_text(k, label);
        k->shift_once = false;
    }
}

void kbd_update(keyboard *k, const void *input)
{
    const app_input *in = input;
    uint32_t d = in->down;

    // Touch: keys are typed when released, like on a phone
    if (in->touching)
    {
        int key = kbd_key_at(k, in->tx, in->ty);
        k->touch_key = key;
        k->touching = true;
    }
    else if (k->touching)
    {
        k->touching = false;
        kbd_press(k, k->touch_key);
        k->touch_key = -1;
    }

    // Buttons
    if (d & IN_B)
        backspace(k);
    if (d & IN_START)
        k->done = true;
    if ((d & IN_SELECT) && k->cancellable)
        k->cancelled = true;
    if ((d & IN_Y) && k->password)
        k->reveal = !k->reveal;
    if ((d & IN_L) && k->mode == KBD_TEXT)
        kbd_press(k, KEY_SHIFT);
    if ((d & IN_R) && qwerty(k))
        kbd_press(k, KEY_SYM);

    // D-pad cursor
    if (d & IN_UP && k->cur_row > 0)
        k->cur_row--;
    if (d & IN_DOWN && k->cur_row < KBD_ROWS)
        k->cur_row++;
    if (d & IN_LEFT && k->cur_col > 0)
        k->cur_col--;
    if (d & IN_RIGHT)
        k->cur_col++;
    int cols = cols_in_row(k, k->cur_row);
    if (k->cur_col >= cols)
        k->cur_col = cols - 1;
    if (d & IN_A)
        kbd_press(k, key_index(k, k->cur_row, k->cur_col));
}

void kbd_draw(keyboard *k, color_t *top, color_t *bottom)
{
    // Top screen: title, text field and help
    gfx_clear(top, C_BG);
    gfx_rect(top, 0, 0, GFX_W, 18, C_PANEL);
    gfx_text(top, 6, 5, "DSHA setup", C_MUTED, 1, 0);
    gfx_text_wrap(top, 8, 26, GFX_W - 16, 2, 18, k->title, C_ACCENT, 2);

    char shown[256];
    if (k->password && !k->reveal)
    {
        size_t n = 0;
        for (const char *p = k->text; utf8_next(&p) != 0 && n < 60;)
            shown[n++] = '*';
        shown[n] = '\0';
    }
    else
    {
        snprintf(shown, sizeof(shown), "%s", k->text);
    }

    // Text box: show the end of the text if it doesn't fit
    gfx_rect(top, 6, 66, GFX_W - 12, 22, C_PANEL);
    gfx_frame(top, 6, 66, GFX_W - 12, 22, C_ACCENT);
    const int max_chars = (GFX_W - 24) / GFX_CHAR_W - 1;
    const char *start = shown;
    int chars = 0;
    for (const char *p = shown; utf8_next(&p) != 0;)
        chars++;
    while (chars > max_chars)
    {
        utf8_next(&start);
        chars--;
    }
    int w = gfx_text(top, 12, 73, start, C_TEXT, 1, 0);
    gfx_rect(top, 12 + w + 1, 71, 2, 12, C_ACCENT); // Cursor

    gfx_text_wrap(top, 8, 96, GFX_W - 16, 5, 11, k->hint, C_MUTED, 1);

    char legend[96];
    snprintf(legend, sizeof(legend), "B del  START ok%s%s",
             k->password ? "  Y show" : "",
             k->cancellable ? "  SELECT back" : "");
    gfx_rect(top, 0, 176, GFX_W, 16, C_PANEL);
    gfx_text(top, 6, 180, legend, C_MUTED, 1, GFX_W - 12);

    // Bottom screen: keys
    gfx_clear(bottom, C_BG);
    int cursor_key = key_index(k, k->cur_row, k->cur_col);

    for (int key = 0; key < KEY_COUNT; key++)
    {
        rect r;
        if (!key_rect(k, key, &r))
            continue;

        bool special = key >= KEY_SHIFT;
        bool active = (key == KEY_SHIFT && k->mode == KBD_TEXT
                       && (k->layer == 1 || k->shift_once))
                      || (key == KEY_SYM && k->layer == 2);
        color_t bg = key == k->touch_key ? C_ACCENT
                     : key == KEY_OK     ? C_ACCENT
                     : active            ? C_TEXT
                     : special           ? C_KEY_SPEC
                                         : C_KEY;
        color_t fg = active && key != k->touch_key ? C_BG : C_TEXT;

        gfx_round_rect(bottom, r.x, r.y, r.w, r.h, bg);
        const char *label = key_label(k, key);
        int scale = (!special && k->mode == KBD_NUMERIC) ? 2 : 1;
        if (!special && qwerty(k))
            scale = 2;
        int tw = gfx_text_width(label, scale);
        gfx_text(bottom, r.x + (r.w - tw) / 2 + 1,
                 r.y + (r.h - 8 * scale) / 2, label, fg, scale, 0);

        if (key == cursor_key)
            gfx_frame(bottom, r.x, r.y, r.w, r.h, C_WHITE);
    }
}

// Picker
// ------

#define ROW_H 34
#define LIST_Y 2
#define ACTIONS_Y 142

void picker_init(picker *p, const char *title, const char *hint)
{
    memset(p, 0, sizeof(*p));
    snprintf(p->title, sizeof(p->title), "%s", title);
    snprintf(p->hint, sizeof(p->hint), "%s", hint ? hint : "");
    snprintf(p->empty_text, sizeof(p->empty_text), "(nothing found yet)");
    p->progress = -1;
    p->chosen = -1;
    p->action = -1;
}

void picker_add(picker *p, const char *item, const char *detail)
{
    if (p->count >= PICKER_MAX_ITEMS)
        return;
    snprintf(p->items[p->count], sizeof(p->items[0]), "%s", item);
    snprintf(p->details[p->count], sizeof(p->details[0]), "%s",
             detail ? detail : "");
    p->count++;
}

void picker_set_actions(picker *p, const char *a0, const char *a1)
{
    snprintf(p->actions[0], sizeof(p->actions[0]), "%s", a0 ? a0 : "");
    snprintf(p->actions[1], sizeof(p->actions[1]), "%s", a1 ? a1 : "");
}

bool picker_finished(const picker *p)
{
    return p->chosen >= 0 || p->action >= 0 || p->cancelled;
}

static int num_actions(const picker *p)
{
    return (p->actions[0][0] ? 1 : 0) + (p->actions[1][0] ? 1 : 0);
}

static rect action_rect(const picker *p, int i)
{
    if (num_actions(p) == 1 || !p->actions[1][0])
        return (rect){ 4, ACTIONS_Y + 4, GFX_W - 8, 42 };
    if (!p->actions[0][0])
        return (rect){ 4, ACTIONS_Y + 4, GFX_W - 8, 42 };
    return i == 0 ? (rect){ 4, ACTIONS_Y + 4, 122, 42 }
                  : (rect){ 130, ACTIONS_Y + 4, 122, 42 };
}

// Scroll arrows are on the right of the list
#define ARROW_X 222

static void ensure_visible(picker *p)
{
    if (p->selected < p->scroll)
        p->scroll = p->selected;
    if (p->selected >= p->scroll + PICKER_VISIBLE)
        p->scroll = p->selected - PICKER_VISIBLE + 1;
    if (p->scroll < 0)
        p->scroll = 0;
}

static void picker_tap(picker *p, int x, int y)
{
    for (int i = 0; i < 2; i++)
    {
        if (!p->actions[i][0])
            continue;
        rect r = action_rect(p, i);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
        {
            p->action = i;
            return;
        }
    }

    if (y < LIST_Y || y >= LIST_Y + PICKER_VISIBLE * ROW_H)
        return;

    bool scrollable = p->count > PICKER_VISIBLE;
    if (scrollable && x >= ARROW_X)
    {
        int max_scroll = p->count - PICKER_VISIBLE;
        if (y < LIST_Y + PICKER_VISIBLE * ROW_H / 2)
            p->scroll = p->scroll > 0 ? p->scroll - 1 : 0;
        else if (p->scroll < max_scroll)
            p->scroll++;
        return;
    }

    int idx = p->scroll + (y - LIST_Y) / ROW_H;
    if (idx >= 0 && idx < p->count)
    {
        p->selected = idx;
        p->chosen = idx;
    }
}

void picker_update(picker *p, const void *input)
{
    const app_input *in = input;
    uint32_t d = in->down;

    if (in->touching && !p->touching)
    {
        p->touching = true;
        p->touch_x = in->tx;
        p->touch_y = in->ty;
    }
    else if (!in->touching && p->touching)
    {
        p->touching = false;
        picker_tap(p, p->touch_x, p->touch_y);
    }

    if ((d & IN_UP) && p->selected > 0)
    {
        p->selected--;
        ensure_visible(p);
    }
    if ((d & IN_DOWN) && p->selected < p->count - 1)
    {
        p->selected++;
        ensure_visible(p);
    }

    if ((d & IN_A) && p->count > 0)
        p->chosen = p->selected;
    if ((d & IN_X) && p->actions[0][0])
        p->action = 0;
    if ((d & IN_Y) && p->actions[1][0])
        p->action = 1;
    if ((d & IN_B) && p->cancellable)
        p->cancelled = true;
}

void picker_draw(picker *p, color_t *top, color_t *bottom)
{
    // Top screen
    gfx_clear(top, C_BG);
    gfx_rect(top, 0, 0, GFX_W, 18, C_PANEL);
    gfx_text(top, 6, 5, "DSHA setup", C_MUTED, 1, 0);
    int lines = gfx_text_wrap(top, 8, 26, GFX_W - 16, 2, 18, p->title,
                              C_ACCENT, 2);
    int y = 26 + lines * 18 + 6;

    if (p->big_text[0])
    {
        gfx_rect(top, 4, y - 2, GFX_W - 8, 26, C_PANEL);
        int scale = gfx_text_width(p->big_text, 2) <= GFX_W - 16 ? 2 : 1;
        gfx_text_centered(top, 4, y + (scale == 2 ? 3 : 7), GFX_W - 8,
                          p->big_text, C_TEXT, scale);
        y += 32;
    }
    if (p->big_text2[0])
    {
        gfx_rect(top, 4, y - 2, GFX_W - 8, 26, C_PANEL);
        gfx_text_centered(top, 4, y + 3, GFX_W - 8, p->big_text2, C_ACCENT, 2);
        y += 32;
    }

    if (p->progress >= 0)
    {
        gfx_rect(top, 8, y, GFX_W - 16, 6, C_PANEL_HI);
        gfx_rect(top, 8, y, (GFX_W - 16) * p->progress / 100, 6, C_ACCENT);
        y += 12;
    }

    gfx_text_wrap(top, 8, y, GFX_W - 16, (176 - y) / 11, 11, p->hint,
                  C_TEXT, 1);

    char legend[96] = "";
    if (p->count > 0)
        snprintf(legend, sizeof(legend), "A choose ");
    if (p->actions[0][0])
        snprintf(legend + strlen(legend), sizeof(legend) - strlen(legend),
                 " X %s ", p->actions[0]);
    if (p->actions[1][0])
        snprintf(legend + strlen(legend), sizeof(legend) - strlen(legend),
                 " Y %s ", p->actions[1]);
    if (p->cancellable)
        strncat(legend, " B back", sizeof(legend) - strlen(legend) - 1);
    gfx_rect(top, 0, 176, GFX_W, 16, C_PANEL);
    gfx_text(top, 6, 180, legend, C_MUTED, 1, GFX_W - 12);

    // Bottom screen: list
    gfx_clear(bottom, C_BG);
    bool scrollable = p->count > PICKER_VISIBLE;
    int list_w = scrollable ? ARROW_X - 6 : GFX_W - 8;

    for (int i = 0; i < PICKER_VISIBLE; i++)
    {
        int idx = p->scroll + i;
        if (idx >= p->count)
            break;
        int ry = LIST_Y + i * ROW_H;
        bool sel = idx == p->selected;
        gfx_round_rect(bottom, 4, ry, list_w, ROW_H - 3, sel ? C_PANEL_HI
                                                              : C_PANEL);
        gfx_text(bottom, 12, ry + 6, p->items[idx], C_TEXT, 1, list_w - 16);
        gfx_text(bottom, 12, ry + 18, p->details[idx], C_MUTED, 1,
                 list_w - 16);
        if (sel)
            gfx_frame(bottom, 4, ry, list_w, ROW_H - 3, C_WHITE);
    }

    if (p->count == 0 && p->empty_text[0] == '\0')
    {
        // Nothing to list: show the logo
        int lx = 100, ly = 22;
        for (int i = 0; i < 24; i++)
            gfx_hline(bottom, lx + 28 - i - 4, ly + i, 8 + i * 2, C_ACCENT);
        gfx_rect(bottom, lx + 6, ly + 24, 44, 26, C_ACCENT);
        gfx_rect(bottom, lx + 22, ly + 34, 12, 16, C_BG);
        gfx_text_centered(bottom, 0, 84, GFX_W, "DSHA", C_TEXT, 3);
        gfx_text_centered(bottom, 0, 112, GFX_W, "v" DSHA_VERSION, C_MUTED, 1);
    }
    else if (p->count == 0)
    {
        gfx_text_wrap(bottom, 16, 50, GFX_W - 32, 6, 11, p->empty_text,
                      C_MUTED, 1);
    }

    if (scrollable)
    {
        int h = PICKER_VISIBLE * ROW_H - 3;
        gfx_round_rect(bottom, ARROW_X, LIST_Y, 30, h / 2 - 1, C_PANEL);
        gfx_round_rect(bottom, ARROW_X, LIST_Y + h / 2 + 1, 30, h / 2 - 1,
                       C_PANEL);
        gfx_text(bottom, ARROW_X + 12, LIST_Y + h / 4 - 4, "^", C_TEXT, 1, 0);
        gfx_text(bottom, ARROW_X + 12, LIST_Y + 3 * h / 4 - 4, "v", C_TEXT, 1,
                 0);
    }

    for (int i = 0; i < 2; i++)
    {
        if (!p->actions[i][0])
            continue;
        rect r = action_rect(p, i);
        gfx_round_rect(bottom, r.x, r.y, r.w, r.h, i == 0 ? C_ACCENT
                                                          : C_KEY_SPEC);
        gfx_text_centered(bottom, r.x, r.y + r.h / 2 - 4, r.w, p->actions[i],
                          C_TEXT, 1);
    }
}
