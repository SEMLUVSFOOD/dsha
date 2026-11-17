// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// platform.h implementation for running the core on a desktop OS (tests).

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>

#include "platform.h"

void platform_yield(void)
{
    struct timespec ts = { 0, 1000000 }; // 1 ms
    nanosleep(&ts, NULL);
}

uint32_t platform_millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

uint32_t platform_random(void)
{
    uint32_t v = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f)
    {
        if (fread(&v, sizeof(v), 1, f) != 1)
            v = 0;
        fclose(f);
    }
    return v ^ platform_millis();
}

void platform_entropy(unsigned char *out, size_t len)
{
    FILE *f = fopen("/dev/urandom", "rb");
    size_t n = f ? fread(out, 1, len, f) : 0;
    if (f)
        fclose(f);
    for (; n < len; n++)
        out[n] = (unsigned char)platform_millis(); // Not reached in practice
}

int platform_run_big_stack(int (*fn)(void *), void *arg)
{
    return fn(arg); // Desktop threads have megabytes of stack
}
