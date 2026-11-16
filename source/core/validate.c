// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <string.h>

#include "validate.h"

bool valid_entity_id(const char *s)
{
    size_t len = strlen(s);
    if (len < 3 || len > 63)
        return false;

    int dots = 0;
    for (size_t i = 0; i < len; i++)
    {
        char c = s[i];
        if (c == '.')
        {
            if (i == 0 || i == len - 1)
                return false;
            dots++;
        }
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                   || c == '_'))
        {
            return false;
        }
    }
    return dots == 1;
}

bool valid_host(const char *s)
{
    size_t len = strlen(s);
    if (len == 0 || len > 127)
        return false;
    for (size_t i = 0; i < len; i++)
    {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'
              || c == ':'))
            return false;
    }
    return s[0] != '-' && s[0] != '.';
}

bool valid_token(const char *s)
{
    size_t len = strlen(s);
    if (len == 0 || len > 511)
        return false;
    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)s[i];
        if (c <= 0x20 || c >= 0x7F)
            return false;
    }
    return true;
}

bool valid_line(const char *s)
{
    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7F)
            return false;
    }
    return true;
}

void sanitize_line(char *s)
{
    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7F)
            *s = ' ';
    }
}

bool is_private_ipv4(uint32_t ip)
{
    uint8_t a = (uint8_t)(ip >> 24);
    uint8_t b = (uint8_t)(ip >> 16);

    return a == 10 || a == 127
           || (a == 172 && b >= 16 && b <= 31)
           || (a == 192 && b == 168)
           || (a == 169 && b == 254)
           || (a == 100 && b >= 64 && b <= 127);
}
