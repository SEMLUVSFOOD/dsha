// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Extracts a few values from a JSON document by path, on top of the streaming
// parser. Paths use dots for object keys and numbers for array indices, like
// "errors.base" or "providers.0.type". Values are returned as text.

#ifndef DSHA_JSON_GET_H
#define DSHA_JSON_GET_H

#include <stdbool.h>
#include <stddef.h>

#include "json_stream.h"

#define JSON_GET_MAX_FIELDS 8

typedef struct {
    const char *path;
    char *out;      // Destination buffer (set to "" if the path isn't found)
    size_t out_len;
    bool found;
} json_field;

typedef struct {
    json_parser parser;
    json_field *fields;
    int num_fields;

    // Current path, as a stack of components
    char keys[JSON_MAX_DEPTH][40];
    int index[JSON_MAX_DEPTH]; // Next index for arrays
} json_getter;

void json_get_begin(json_getter *g, json_field *fields, int num_fields);
bool json_get_feed(json_getter *g, const char *data, size_t len);
bool json_get_end(json_getter *g);

// Convenience: parse a whole document in memory
bool json_get(const char *doc, json_field *fields, int num_fields);

#endif // DSHA_JSON_GET_H
