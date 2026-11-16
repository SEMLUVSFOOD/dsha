// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Widgets used by the setup wizard: an on-screen keyboard and a touch list
// picker. Both draw the top screen (title, text, help) and the bottom screen
// (keys or list), and work with the touch screen and with the buttons.

#ifndef DSHA_WIDGETS_H
#define DSHA_WIDGETS_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"


// Keyboard
// --------

typedef enum {
    KBD_TEXT,    // Full QWERTY keyboard with shift and symbols
    KBD_NUMERIC, // Digits, '.' and ':' (codes, IP addresses)
} kbd_mode;

#define KBD_ROWS 4
#define KBD_COLS 10

typedef struct {
    char title[48];
    char hint[200];
    char text[256];
    int max_len;
    kbd_mode mode;
    bool password;      // Show '*' instead of the characters...
    bool reveal;        // ...unless the user asked to see them (Y)
    bool cancellable;

    int layer;          // 0 lowercase, 1 uppercase, 2 symbols
    bool shift_once;    // Uppercase for one character only

    // Key under the cursor (D-pad) and key being touched
    int cur_row, cur_col;
    int touch_key;      // -1 if none
    bool touching;

    bool done;
    bool cancelled;
} keyboard;

void kbd_init(keyboard *k, const char *title, const char *initial,
              int max_len, kbd_mode mode, bool password, bool cancellable);

// input is an app_input (see app.h)
void kbd_update(keyboard *k, const void *input);
void kbd_draw(keyboard *k, color_t *top, color_t *bottom);

// Exposed for the tests: returns the key at a touch position, or -1
int kbd_key_at(const keyboard *k, int x, int y);
// Presses a key by index
void kbd_press(keyboard *k, int key);

// Picker
// ------

#define PICKER_MAX_ITEMS 16
#define PICKER_VISIBLE 4

typedef struct {
    char title[48];
    char hint[240];
    char big_text[64];      // Large text on the top screen (an URL...)
    char empty_text[64];    // Shown on the bottom screen if there are no items
    int progress;           // 0-100, or -1 to hide the progress bar

    char items[PICKER_MAX_ITEMS][48];
    char details[PICKER_MAX_ITEMS][48];
    int count;

    char actions[2][24];    // Buttons under the list ("" = none)

    int selected;           // Highlighted item (D-pad)
    int scroll;

    int chosen;             // Result: item index, or -1
    int action;             // Result: 0/1 if an action button was pressed
    bool cancelled;         // B pressed (if cancellable)
    bool cancellable;

    bool touching;
    int touch_x, touch_y;
} picker;

void picker_init(picker *p, const char *title, const char *hint);
void picker_add(picker *p, const char *item, const char *detail);
void picker_set_actions(picker *p, const char *a0, const char *a1);
void picker_update(picker *p, const void *input);
void picker_draw(picker *p, color_t *top, color_t *bottom);
bool picker_finished(const picker *p);

#endif // DSHA_WIDGETS_H
