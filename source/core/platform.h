// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Small set of functions that each platform (Nintendo DS, or the desktop test
// harness) has to provide to the portable core.

#ifndef DSHA_PLATFORM_H
#define DSHA_PLATFORM_H

#include <stdint.h>

// Give other threads (on the DS: the lwIP network thread) some CPU time. Called
// in every loop that waits for network data.
void platform_yield(void);

// Monotonic milliseconds counter. It may wrap around.
uint32_t platform_millis(void);

#endif // DSHA_PLATFORM_H
