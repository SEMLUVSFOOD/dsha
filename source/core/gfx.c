// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <stdbool.h>
#include <string.h>

#include "font8x8.h"
#include "gfx.h"

uint32_t utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = p[0];

    if (c == 0)
        return 0;

    int extra;
    if (c < 0x80)
    {
        *s += 1;
        return c;
    }
    else if ((c & 0xE0) == 0xC0)
    {
        extra = 1;
        c &= 0x1F;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        extra = 2;
        c &= 0x0F;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        extra = 3;
        c &= 0x07;
    }
    else
    {
        *s += 1;
        return 0xFFFD;
    }

    for (int i = 1; i <= extra; i++)
    {
        if ((p[i] & 0xC0) != 0x80)
        {
            *s += i;
            return 0xFFFD;
        }
        c = (c << 6) | (p[i] & 0x3F);
    }

    *s += 1 + extra;
    return c;
}

int utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80)
    {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800)
    {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000)
    {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

void gfx_clear(color_t *fb, color_t c)
{
    for (int i = 0; i < GFX_W * GFX_H; i++)
        fb[i] = c;
}

void gfx_rect(color_t *fb, int x, int y, int w, int h, color_t c)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > GFX_W ? GFX_W : x + w;
    int y1 = y + h > GFX_H ? GFX_H : y + h;

    for (int j = y0; j < y1; j++)
    {
        color_t *row = fb + j * GFX_W;
        for (int i = x0; i < x1; i++)
            row[i] = c;
    }
}

void gfx_hline(color_t *fb, int x, int y, int w, color_t c)
{
    gfx_rect(fb, x, y, w, 1, c);
}

void gfx_frame(color_t *fb, int x, int y, int w, int h, color_t c)
{
    gfx_rect(fb, x, y, w, 1, c);
    gfx_rect(fb, x, y + h - 1, w, 1, c);
    gfx_rect(fb, x, y, 1, h, c);
    gfx_rect(fb, x + w - 1, y, 1, h, c);
}

void gfx_round_rect(color_t *fb, int x, int y, int w, int h, color_t c)
{
    gfx_rect(fb, x + 2, y, w - 4, 1, c);
    gfx_rect(fb, x + 1, y + 1, w - 2, 1, c);
    gfx_rect(fb, x, y + 2, w, h - 4, c);
    gfx_rect(fb, x + 1, y + h - 2, w - 2, 1, c);
    gfx_rect(fb, x + 2, y + h - 1, w - 4, 1, c);
}

static void draw_glyph(color_t *fb, int x, int y, uint32_t cp, color_t c,
                       int scale)
{
    const uint8_t *g = font8x8_glyph(cp);

    for (int row = 0; row < 8; row++)
    {
        uint8_t bits = g[row];
        if (bits == 0)
            continue;

        for (int col = 0; col < 8; col++)
        {
            if (!(bits & (1 << col)))
                continue;

            int px = x + col * scale;
            int py = y + row * scale;

            for (int sy = 0; sy < scale; sy++)
            {
                int yy = py + sy;
                if (yy < 0 || yy >= GFX_H)
                    continue;
                for (int sx = 0; sx < scale; sx++)
                {
                    int xx = px + sx;
                    if (xx < 0 || xx >= GFX_W)
                        continue;
                    fb[yy * GFX_W + xx] = c;
                }
            }
        }
    }
}

// Emoji, symbols and invisible modifiers are skipped instead of being drawn
// as '?', as they are quite common in friendly names.
static bool skip_cp(uint32_t cp)
{
    return (cp >= 0x2000 && cp <= 0x2BFF)   // Punctuation, arrows, symbols
           || (cp >= 0xFE00 && cp <= 0xFE0F) // Variation selectors
           || cp >= 0x1F000                  // Emoji
           || cp == 0x200D;                  // Zero width joiner
}

static uint32_t map_cp(uint32_t cp)
{
    // A few common typographic characters have ASCII equivalents
    switch (cp)
    {
        case 0x2018: case 0x2019: return '\'';
        case 0x201C: case 0x201D: return '"';
        case 0x2013: case 0x2014: return '-';
        case 0x2026: return '.';
        default: return cp;
    }
}

// Next drawable code point (0 at the end of the string)
static uint32_t next_drawable(const char **s)
{
    while (1)
    {
        uint32_t cp = map_cp(utf8_next(s));
        if (cp == 0 || !skip_cp(cp))
            return cp;
    }
}

int gfx_text_width(const char *utf8, int scale)
{
    int n = 0;
    while (next_drawable(&utf8) != 0)
        n++;
    return n * GFX_CHAR_W * scale;
}

int gfx_text(color_t *fb, int x, int y, const char *utf8, color_t c, int scale,
             int max_w)
{
    const int adv = GFX_CHAR_W * scale;
    int total = gfx_text_width(utf8, scale);
    int limit = -1; // Max number of chars to draw before the ".." suffix

    if (max_w > 0 && total > max_w)
    {
        limit = (max_w - 2 * adv) / adv;
        if (limit < 0)
            limit = 0;
    }

    int drawn = 0;
    int cx = x;
    const char *p = utf8;
    uint32_t cp;

    while ((cp = next_drawable(&p)) != 0)
    {
        if (limit >= 0 && drawn >= limit)
            break;
        draw_glyph(fb, cx, y, cp, c, scale);
        cx += adv;
        drawn++;
    }

    if (limit >= 0)
    {
        draw_glyph(fb, cx, y, '.', c, scale);
        cx += adv;
        draw_glyph(fb, cx, y, '.', c, scale);
        cx += adv;
    }

    return cx - x;
}

void gfx_text_centered(color_t *fb, int x, int y, int w, const char *utf8,
                       color_t c, int scale)
{
    int tw = gfx_text_width(utf8, scale);
    if (tw > w)
        tw = w;
    gfx_text(fb, x + (w - tw) / 2, y, utf8, c, scale, w);
}

int gfx_text_wrap(color_t *fb, int x, int y, int w, int max_lines, int line_h,
                  const char *utf8, color_t c, int scale)
{
    const int adv = GFX_CHAR_W * scale;
    const int cols = w / adv;
    if (cols <= 0 || max_lines <= 0)
        return 0;

    char line[256];
    int lines = 0;
    const char *p = utf8;

    while (*p != '\0' && lines < max_lines)
    {
        // Skip leading spaces of the line
        while (*p == ' ')
            p++;
        if (*p == '\0')
            break;

        // Find how many bytes fit in this line, preferring to break at spaces
        const char *scan = p;
        const char *last_space = NULL;
        int count = 0;
        int newline = 0;

        while (*scan != '\0' && count < cols)
        {
            if (*scan == '\n')
            {
                newline = 1;
                break;
            }
            const char *prev = scan;
            uint32_t cp = map_cp(utf8_next(&scan));
            if (skip_cp(cp))
                continue;
            if (cp == ' ')
                last_space = prev;
            count++;
        }

        const char *end = scan;
        if (!newline && *scan != '\0' && *scan != ' ' && *scan != '\n'
            && last_space != NULL)
            end = last_space;

        size_t len = (size_t)(end - p);
        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        int is_last_line = (lines == max_lines - 1);
        int has_more = (*end != '\0');
        if (is_last_line && has_more && !newline)
        {
            // Show the truncation in the last line
            const char *rest = p;
            size_t rl = strlen(rest);
            if (rl >= sizeof(line))
                rl = sizeof(line) - 1;
            memcpy(line, rest, rl);
            line[rl] = '\0';
            char *nl = strchr(line, '\n');
            if (nl)
                *nl = '\0';
        }

        gfx_text(fb, x, y + lines * line_h, line, c, scale, w);
        lines++;

        p = end;
        if (*p == '\n')
            p++;
    }

    return lines;
}
