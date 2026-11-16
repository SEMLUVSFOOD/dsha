// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <string.h>

#include "gfx.h" // utf8_encode()
#include "json_stream.h"

enum {
    ST_VALUE,       // Expecting a value (or end of array)
    ST_AFTER_VALUE, // Expecting ',' or the end of the container
    ST_COLON,       // Expecting ':' after a key
    ST_STRING,
    ST_STRING_ESC,
    ST_STRING_UNI,
    ST_NUMBER,
    ST_LITERAL,
    ST_DONE,
};

void json_init(json_parser *p, json_callback cb, void *user)
{
    memset(p, 0, sizeof(*p));
    p->cb = cb;
    p->user = user;
    p->state = ST_VALUE;
}

static void buf_putc(json_parser *p, char c)
{
    if (p->buf_len < JSON_MAX_STRING - 1)
        p->buf[p->buf_len++] = c;
}

static void buf_put_cp(json_parser *p, unsigned int cp)
{
    char tmp[4];
    int n = utf8_encode(cp, tmp);
    // Don't split multi-byte characters when truncating
    if (p->buf_len + n >= JSON_MAX_STRING)
    {
        p->buf_len = JSON_MAX_STRING - 1;
        return;
    }
    for (int i = 0; i < n; i++)
        p->buf[p->buf_len++] = tmp[i];
}

static void emit(json_parser *p, json_event ev, const char *value)
{
    if (p->cb)
        p->cb(p->user, ev, value, p->depth);
}

// Called after a complete value has been parsed
static void value_done(json_parser *p)
{
    if (p->depth == 0)
        p->state = ST_DONE;
    else
        p->state = ST_AFTER_VALUE;
}

static void start_container(json_parser *p, bool object)
{
    if (p->depth >= JSON_MAX_DEPTH - 1)
    {
        p->error = true;
        return;
    }
    emit(p, object ? JSON_EV_OBJECT_START : JSON_EV_ARRAY_START, NULL);
    p->depth++;
    p->is_object[p->depth] = object;
    p->expect_key[p->depth] = object;
    p->state = ST_VALUE;
}

static void end_container(json_parser *p, bool object)
{
    if (p->depth == 0 || p->is_object[p->depth] != object)
    {
        p->error = true;
        return;
    }
    p->depth--;
    emit(p, object ? JSON_EV_OBJECT_END : JSON_EV_ARRAY_END, NULL);
    value_done(p);
}

static void finish_number(json_parser *p)
{
    p->buf[p->buf_len] = '\0';
    emit(p, JSON_EV_NUMBER, p->buf);
    value_done(p);
}

static void finish_string(json_parser *p)
{
    p->buf[p->buf_len] = '\0';
    if (p->buf_is_key)
    {
        emit(p, JSON_EV_KEY, p->buf);
        p->expect_key[p->depth] = 0;
        p->state = ST_COLON;
    }
    else
    {
        emit(p, JSON_EV_STRING, p->buf);
        if (p->depth > 0 && p->is_object[p->depth])
            p->expect_key[p->depth] = 1;
        value_done(p);
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool json_feed(json_parser *p, const char *data, size_t len)
{
    for (size_t i = 0; i < len && !p->error; i++)
    {
        char c = data[i];

        switch (p->state)
        {
            case ST_DONE:
                if (!is_ws(c))
                    p->error = true;
                break;

            case ST_VALUE:
            {
                if (is_ws(c))
                    break;

                bool in_object = p->depth > 0 && p->is_object[p->depth];

                if (in_object && p->expect_key[p->depth])
                {
                    if (c == '"')
                    {
                        p->buf_len = 0;
                        p->buf_is_key = true;
                        p->state = ST_STRING;
                    }
                    else if (c == '}')
                    {
                        end_container(p, true);
                    }
                    else
                    {
                        p->error = true;
                    }
                    break;
                }

                if (c == '{')
                    start_container(p, true);
                else if (c == '[')
                    start_container(p, false);
                else if (c == ']' && p->depth > 0 && !p->is_object[p->depth])
                    end_container(p, false);
                else if (c == '"')
                {
                    p->buf_len = 0;
                    p->buf_is_key = false;
                    p->state = ST_STRING;
                }
                else if (c == '-' || (c >= '0' && c <= '9'))
                {
                    p->buf_len = 0;
                    buf_putc(p, c);
                    p->state = ST_NUMBER;
                }
                else if (c == 't')
                {
                    p->lit = "true";
                    p->lit_pos = 1;
                    p->state = ST_LITERAL;
                }
                else if (c == 'f')
                {
                    p->lit = "false";
                    p->lit_pos = 1;
                    p->state = ST_LITERAL;
                }
                else if (c == 'n')
                {
                    p->lit = "null";
                    p->lit_pos = 1;
                    p->state = ST_LITERAL;
                }
                else
                {
                    p->error = true;
                }
                break;
            }

            case ST_AFTER_VALUE:
                if (is_ws(c))
                    break;
                if (c == ',')
                {
                    if (p->is_object[p->depth])
                        p->expect_key[p->depth] = 1;
                    p->state = ST_VALUE;
                }
                else if (c == '}')
                    end_container(p, true);
                else if (c == ']')
                    end_container(p, false);
                else
                    p->error = true;
                break;

            case ST_COLON:
                if (is_ws(c))
                    break;
                if (c == ':')
                    p->state = ST_VALUE;
                else
                    p->error = true;
                break;

            case ST_STRING:
                if (c == '"')
                    finish_string(p);
                else if (c == '\\')
                    p->state = ST_STRING_ESC;
                else
                    buf_putc(p, c);
                break;

            case ST_STRING_ESC:
                p->state = ST_STRING;
                switch (c)
                {
                    case 'n': buf_putc(p, '\n'); break;
                    case 't': buf_putc(p, '\t'); break;
                    case 'r': buf_putc(p, '\r'); break;
                    case 'b': buf_putc(p, '\b'); break;
                    case 'f': buf_putc(p, '\f'); break;
                    case 'u':
                        p->uni_digits = 0;
                        p->uni_value = 0;
                        p->state = ST_STRING_UNI;
                        break;
                    default: buf_putc(p, c); break; // \" \\ \/
                }
                break;

            case ST_STRING_UNI:
            {
                int h = hexval(c);
                if (h < 0)
                {
                    p->error = true;
                    break;
                }
                p->uni_value = (p->uni_value << 4) | (unsigned int)h;
                if (++p->uni_digits < 4)
                    break;

                unsigned int cp = p->uni_value;
                p->state = ST_STRING;

                if (cp >= 0xD800 && cp <= 0xDBFF)
                {
                    p->uni_high = cp; // Wait for the low surrogate
                    break;
                }
                if (cp >= 0xDC00 && cp <= 0xDFFF)
                {
                    if (p->uni_high)
                        cp = 0x10000 + ((p->uni_high - 0xD800) << 10)
                             + (cp - 0xDC00);
                    else
                        cp = 0xFFFD;
                }
                p->uni_high = 0;
                buf_put_cp(p, cp);
                break;
            }

            case ST_NUMBER:
                if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E'
                    || c == '+' || c == '-')
                {
                    buf_putc(p, c);
                    break;
                }
                finish_number(p);
                // Reprocess this character in the new state
                i--;
                break;

            case ST_LITERAL:
                if (c != p->lit[p->lit_pos])
                {
                    p->error = true;
                    break;
                }
                p->lit_pos++;
                if (p->lit[p->lit_pos] == '\0')
                {
                    if (p->lit[0] == 't')
                        emit(p, JSON_EV_TRUE, "true");
                    else if (p->lit[0] == 'f')
                        emit(p, JSON_EV_FALSE, "false");
                    else
                        emit(p, JSON_EV_NULL, "null");
                    value_done(p);
                }
                break;
        }

        // After a value inside an object, the next string is a key
        if (p->state == ST_AFTER_VALUE && p->depth > 0
            && p->is_object[p->depth])
            p->expect_key[p->depth] = 1;
    }

    return !p->error;
}

bool json_finish(json_parser *p)
{
    if (p->state == ST_NUMBER && p->depth == 0)
        finish_number(p);
    if (p->state != ST_DONE)
        p->error = true;
    return !p->error;
}
