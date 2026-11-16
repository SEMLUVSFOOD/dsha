// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#ifndef DSHA_FONT8X8_H
#define DSHA_FONT8X8_H

#include <stdint.h>

// 128 basic latin glyphs + 96 Latin-1 supplement glyphs (U+00A0 - U+00FF)
#define FONT8X8_GLYPHS 224

extern const uint8_t font8x8_data[FONT8X8_GLYPHS][8];

// Returns the glyph for a Unicode code point. Unsupported code points are
// mapped to '?'.
static inline const uint8_t *font8x8_glyph(uint32_t cp)
{
    if (cp < 128)
        return font8x8_data[cp];
    if (cp >= 0xA0 && cp <= 0xFF)
        return font8x8_data[128 + (cp - 0xA0)];
    return font8x8_data['?'];
}

#endif // DSHA_FONT8X8_H
