// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Minimal HTTP/1.1 client on top of BSD sockets, with optional TLS (HTTPS,
// see tls.h). It supports keep-alive, Content-Length and chunked responses.
// Response bodies are streamed to a callback so they never need to fit in RAM.

#ifndef DSHA_HTTP_H
#define DSHA_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tls.h"

// Return false to abort the transfer
typedef bool (*http_data_cb)(void *user, const char *data, size_t len);

typedef struct {
    char host[128];
    int port;
    int timeout_ms; // Max time without receiving any data

    // HTTPS. The certificate is always verified.
    bool tls;

    // Refuse servers outside the local network (see validate.h). Only for
    // plain HTTP: HTTPS connections are encrypted and may go anywhere.
    bool allow_public;

    int fd;              // Kept open between requests (keep-alive)
    tls_conn *tls_conn;  // TLS session on fd (HTTPS)
    bool addr_valid;     // Cached DNS result
    uint8_t addr[16];    // struct sockaddr_in storage
    size_t addr_len;

    // Statistics, useful for the debug screen
    uint32_t requests;
    uint32_t reconnects;
    uint32_t bytes_received;

    // Error of the last failed request (for messages like "not found")
    char last_error[128];
} http_client;

typedef struct {
    int status;       // HTTP status code, or 0 on network error
    char error[128];  // Human readable error if status == 0
} http_result;

void http_client_init(http_client *c, const char *host, int port);

// Same, for http:// (tls = false) or https:// (tls = true)
void http_client_init_url(http_client *c, const char *host, int port,
                          bool tls);

// Closes the keep-alive connection (if any)
void http_client_close(http_client *c);

// Sends a request. extra_headers can be NULL, otherwise it must contain full
// "Name: value\r\n" lines. body can be NULL; it is sent as JSON. The body of
// successful (2xx) responses is passed to cb (which can be NULL); the body of
// other responses is discarded. Returns true if a complete HTTP response has
// been received (any status code).
bool http_request(http_client *c, const char *method, const char *path,
                  const char *extra_headers, const char *body,
                  http_data_cb cb, void *user, http_result *res);

// Same as http_request(), with a custom Content-Type for the body. If
// error_body is true, the body is passed to cb for any status code.
bool http_request_ex(http_client *c, const char *method, const char *path,
                     const char *extra_headers, const char *body,
                     const char *content_type, bool error_body,
                     http_data_cb cb, void *user, http_result *res);

// Appends "value" URL-encoded (application/x-www-form-urlencoded) to out.
void http_urlencode(char *out, size_t out_len, const char *value);

#endif // DSHA_HTTP_H
