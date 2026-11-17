// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Small set of functions that each platform (Nintendo DS, or the desktop test
// harness) has to provide to the portable core.

#ifndef DSHA_PLATFORM_H
#define DSHA_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

// Give other threads (on the DS: the lwIP network thread) some CPU time. Called
// in every loop that waits for network data.
void platform_yield(void);

// Monotonic milliseconds counter. It may wrap around.
uint32_t platform_millis(void);

// Random number for the pairing code of the setup page. It doesn't need to be
// cryptographically strong (the code is short-lived and attempts are
// limited), but it must not be predictable from outside.
uint32_t platform_random(void);

// Fills out with random bytes for cryptographic keys (HTTPS). On the DS this
// is a hash of an entropy pool; it's mixed with Mbed TLS's own sources.
void platform_entropy(unsigned char *out, size_t len);

// Runs fn(arg) on a stack of at least 64 KB and returns its result. The TLS
// handshake needs more stack than the 16 KB of DTCM the DS's main thread has.
int platform_run_big_stack(int (*fn)(void *), void *arg);

#endif // DSHA_PLATFORM_H
