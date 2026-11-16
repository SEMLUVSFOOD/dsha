// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Tiny software renderer for 256x192 16-bit (BGR555) framebuffers, the native
// bitmap format of the Nintendo DS 2D engines.

#ifndef DSHA_GFX_H
#define DSHA_GFX_H

#include <stdint.h>

#define GFX_W 256
#define GFX_H 192

// Horizontal advance of one character of the 8x8 font at scale 1
#define GFX_CHAR_W 7
#define GFX_CHAR_H 8

typedef uint16_t color_t;

// Build a color from 8-bit components. Bit 15 must be set for the DS bitmap
// background to show the pixel.
#define C_RGB(r, g, b) \
    ((color_t)((((r) >> 3) & 31) | ((((g) >> 3) & 31) << 5) | ((((b) >> 3) & 31) << 10) | 0x8000))

// Decode the next UTF-8 code point from *s and advance the pointer. Invalid
// sequences return U+FFFD. Returns 0 at the end of the string.
uint32_t utf8_next(const char **s);

// Encode a code point as UTF-8. Returns the number of bytes written (max 4).
int utf8_encode(uint32_t cp, char *out);

void gfx_clear(color_t *fb, color_t c);
void gfx_rect(color_t *fb, int x, int y, int w, int h, color_t c);
void gfx_frame(color_t *fb, int x, int y, int w, int h, color_t c);
// Filled rectangle with 2 pixel cut corners
void gfx_round_rect(color_t *fb, int x, int y, int w, int h, color_t c);
void gfx_hline(color_t *fb, int x, int y, int w, color_t c);

// Width in pixels of a string rendered at the given scale
int gfx_text_width(const char *utf8, int scale);

// Draws a string. If max_w > 0 and the text doesn't fit, it is truncated and
// ".." is appended. Returns the width in pixels of what was drawn.
int gfx_text(color_t *fb, int x, int y, const char *utf8, color_t c, int scale,
             int max_w);

// Same as gfx_text(), but centered horizontally inside [x, x + w)
void gfx_text_centered(color_t *fb, int x, int y, int w, const char *utf8,
                       color_t c, int scale);

// Draws word-wrapped text inside a box of width w. At most max_lines lines are
// drawn (the last one is truncated). Returns the number of lines drawn.
int gfx_text_wrap(color_t *fb, int x, int y, int w, int max_lines,
                  int line_h, const char *utf8, color_t c, int scale);

#endif // DSHA_GFX_H
