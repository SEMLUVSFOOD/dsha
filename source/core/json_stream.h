// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Incremental (SAX-style) JSON parser. Data can be fed in chunks of any size,
// so responses of several megabytes can be processed with a few hundred bytes
// of RAM. Strings longer than JSON_MAX_STRING are truncated.

#ifndef DSHA_JSON_STREAM_H
#define DSHA_JSON_STREAM_H

#include <stdbool.h>
#include <stddef.h>

#define JSON_MAX_STRING 256
#define JSON_MAX_DEPTH 32

typedef enum {
    JSON_EV_OBJECT_START,
    JSON_EV_OBJECT_END,
    JSON_EV_ARRAY_START,
    JSON_EV_ARRAY_END,
    JSON_EV_KEY,    // value = key name
    JSON_EV_STRING, // value = decoded string (UTF-8)
    JSON_EV_NUMBER, // value = number as text
    JSON_EV_TRUE,
    JSON_EV_FALSE,
    JSON_EV_NULL,
} json_event;

// depth is the nesting level of the container that holds the value: 0 for a
// top level value, 1 for members of the top level object/array, etc. For
// *_START/*_END events it's the depth of the container itself.
typedef void (*json_callback)(void *user, json_event ev, const char *value,
                              int depth);

typedef struct {
    json_callback cb;
    void *user;

    int state;
    int depth;
    // For each depth: 1 if the container is an object, 0 if it's an array
    unsigned char is_object[JSON_MAX_DEPTH];
    // For each depth: 1 if the next string of the object is a key
    unsigned char expect_key[JSON_MAX_DEPTH];

    char buf[JSON_MAX_STRING];
    int buf_len;
    bool buf_is_key;

    // \uXXXX decoding
    int uni_digits;
    unsigned int uni_value;
    unsigned int uni_high; // pending high surrogate

    // Literals (true/false/null)
    const char *lit;
    int lit_pos;

    bool error;
} json_parser;

void json_init(json_parser *p, json_callback cb, void *user);

// Returns false if a syntax error has been found (then it stops parsing).
bool json_feed(json_parser *p, const char *data, size_t len);

// Call when the input has finished, to flush a trailing top-level number.
bool json_finish(json_parser *p);

#endif // DSHA_JSON_STREAM_H
