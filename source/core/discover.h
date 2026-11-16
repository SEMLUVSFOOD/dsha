// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Finds Home Assistant servers in the local network, so the user doesn't need
// to type an IP address. mDNS isn't available on the DS, so it tries a couple
// of common host names and then scans the /24 subnet of the console for the
// Home Assistant port with a few parallel non-blocking connections. Every
// candidate is confirmed with GET /auth/providers.
//
// Usage: discovery_start(), then call discovery_step() every frame until it
// returns false.

#ifndef DSHA_DISCOVER_H
#define DSHA_DISCOVER_H

#include <stdbool.h>
#include <stdint.h>

#define DISCOVERY_SLOTS 6      // Parallel attempts (lwIP on the DS has 8 sockets)
#define DISCOVERY_MAX_FOUND 6
#define DISCOVERY_TIMEOUT_MS 350

typedef struct {
    char host[64];  // Host name if found by name (survives IP changes)
    int port;
    uint32_t ip;    // Host byte order
} ha_found;

typedef struct {
    int port;
    uint32_t self_ip;   // Host byte order
    uint32_t net;       // First address of the /24 (host byte order)
    int next_host;      // Next host number to try (1..254)

    const char *const *names; // Host names to try before scanning
    int next_name;

    struct {
        int fd;         // -1 if the slot is free
        uint32_t ip;
        uint32_t start_ms;
    } slots[DISCOVERY_SLOTS];

    ha_found found[DISCOVERY_MAX_FOUND];
    int num_found;

    int scanned;        // For the progress bar
    int total;
    bool done;
} discovery;

// ip and netmask are IPv4 addresses in host byte order. port is usually 8123.
void discovery_start(discovery *d, uint32_t ip, int port);

// Replaces the list of host names tried first (NULL terminated). Used by the
// tests.
void discovery_set_names(discovery *d, const char *const *names);

// Does a bit of work. Returns false when the scan has finished.
bool discovery_step(discovery *d);

// Stops the scan and closes the sockets
void discovery_stop(discovery *d);

#endif // DSHA_DISCOVER_H
