// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <stdio.h>
#include <string.h>

#include "json_get.h"

// Builds the path of a value at the given depth and compares it with the
// requested fields.
static void match_value(json_getter *g, int depth, const char *value)
{
    char path[256];
    size_t len = 0;
    path[0] = '\0';

    for (int d = 1; d <= depth; d++)
    {
        const char *comp = g->keys[d];
        int n = snprintf(path + len, sizeof(path) - len, "%s%s",
                         len ? "." : "", comp);
        if (n < 0 || (size_t)n >= sizeof(path) - len)
            return;
        len += (size_t)n;
    }

    for (int i = 0; i < g->num_fields; i++)
    {
        json_field *f = &g->fields[i];
        if (!f->found && strcmp(f->path, path) == 0)
        {
            snprintf(f->out, f->out_len, "%s", value);
            f->found = true;
        }
    }
}

static void cb(void *user, json_event ev, const char *value, int depth)
{
    json_getter *g = user;
    json_parser *p = &g->parser;

    // Keys of the container at "depth" are stored in keys[depth]. For arrays
    // the component is the index of the element.
    switch (ev)
    {
        case JSON_EV_KEY:
            snprintf(g->keys[depth], sizeof(g->keys[depth]), "%s", value);
            return;

        case JSON_EV_OBJECT_START:
        case JSON_EV_ARRAY_START:
            if (depth > 0 && !p->is_object[depth])
                snprintf(g->keys[depth], sizeof(g->keys[depth]), "%d",
                         g->index[depth]++);
            if (depth + 1 < JSON_MAX_DEPTH)
                g->index[depth + 1] = 0;
            return;

        case JSON_EV_OBJECT_END:
        case JSON_EV_ARRAY_END:
            return;

        default:
            break;
    }

    // Scalar value
    if (depth > 0 && !p->is_object[depth])
        snprintf(g->keys[depth], sizeof(g->keys[depth]), "%d",
                 g->index[depth]++);
    match_value(g, depth, value);
}

void json_get_begin(json_getter *g, json_field *fields, int num_fields)
{
    memset(g, 0, sizeof(*g));
    g->fields = fields;
    g->num_fields = num_fields;
    for (int i = 0; i < num_fields; i++)
    {
        fields[i].found = false;
        if (fields[i].out_len > 0)
            fields[i].out[0] = '\0';
    }
    json_init(&g->parser, cb, g);
}

bool json_get_feed(json_getter *g, const char *data, size_t len)
{
    return json_feed(&g->parser, data, len);
}

bool json_get_end(json_getter *g)
{
    return json_finish(&g->parser);
}

bool json_get(const char *doc, json_field *fields, int num_fields)
{
    static json_getter g;
    json_get_begin(&g, fields, num_fields);
    return json_get_feed(&g, doc, strlen(doc)) && json_get_end(&g);
}
