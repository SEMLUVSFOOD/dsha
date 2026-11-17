// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// TLS (HTTPS) client connections on top of a connected, non-blocking socket,
// using Mbed TLS. Certificates are always verified: against the built-in root
// certificates (ca_roots.c) and, optionally, the certificates in a PEM file
// chosen by the user (for self-signed certificates or a private CA).

#ifndef DSHA_TLS_H
#define DSHA_TLS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct tls_conn tls_conn;

// Return values of tls_read() and tls_write() besides a byte count
#define TLS_AGAIN  (-1) // Nothing can be done now, try again later
#define TLS_ERROR  (-2) // Fatal error, close the connection

// Extra trusted certificates (PEM file, may contain several). Call before the
// first connection; NULL or a missing file just uses the built-in roots.
// Returns false if the file exists but can't be parsed.
bool tls_set_ca_file(const char *path);

// Only trust the certificates of tls_set_ca_file() (used by the tests).
void tls_use_builtin_roots(bool use);

// Number of trusted certificates (built-in and from the file)
int tls_trusted_count(void);

// Does the TLS handshake on fd (already connected) and checks that the
// certificate is valid for host. Returns NULL on error, with a human readable
// message in err.
tls_conn *tls_connect(int fd, const char *host, int timeout_ms, char *err,
                      size_t err_len);

// Like send()/recv(). tls_read() returns 0 when the server closed the
// connection.
int tls_write(tls_conn *t, const void *data, size_t len);
int tls_read(tls_conn *t, void *buf, size_t len);

// Sends close_notify (best effort) and frees the connection. The socket isn't
// closed.
void tls_close(tls_conn *t);

#endif // DSHA_TLS_H
