// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Minimal unit test helpers.

#ifndef DSHA_TESTLIB_H
#define DSHA_TESTLIB_H

#include <stdio.h>
#include <string.h>

static int tests_failed = 0;
static int tests_run = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        tests_run++;                                                         \
        if (!(cond)) {                                                       \
            tests_failed++;                                                  \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                    \
    } while (0)

#define CHECK_STR(a, b)                                                      \
    do {                                                                     \
        tests_run++;                                                         \
        if (strcmp((a), (b)) != 0) {                                         \
            tests_failed++;                                                  \
            printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__,   \
                   (a), (b));                                                \
        }                                                                    \
    } while (0)

#define TEST_REPORT(name)                                                    \
    do {                                                                     \
        printf("%s: %d checks, %d failed\n", name, tests_run, tests_failed); \
        return tests_failed ? 1 : 0;                                         \
    } while (0)

#endif
