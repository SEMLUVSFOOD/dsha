// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Parser for the dsha config file (a simple "key = value" INI-like format).

#ifndef DSHA_CONFIG_H
#define DSHA_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define CFG_MAX_ENTITIES 128
#define CFG_ENTITY_ID_LEN 64

typedef struct {
    char host[128];
    int port;
    char token[512];         // Long-lived access token...
    char refresh_token[512]; // ...or refresh token from the sign in screen

    // Optional WiFi settings. If ssid is empty, the networks saved in the
    // console's WFC settings are used.
    char wifi_ssid[33];
    char wifi_password[65];

    char title[32];
    int refresh_seconds; // 0 disables auto refresh

    // Explicit list of entities. If empty, all supported entities are loaded.
    int num_entities;
    char entities[CFG_MAX_ENTITIES][CFG_ENTITY_ID_LEN];

    // Optional comma separated list of domains used in automatic mode
    char domains[256];

    // Offline demo with fake entities (no WiFi or server needed)
    bool demo;
} ha_config;

void config_defaults(ha_config *cfg);

// Parse a whole config file from memory. Returns true if the mandatory fields
// (host and token) are present (they aren't needed with demo=1). On error a message is written to err.
bool config_parse(ha_config *cfg, const char *text, char *err, size_t errlen);

// Load and parse a config file from disk.
bool config_load_file(ha_config *cfg, const char *path, char *err,
                      size_t errlen);

// Writes the config to a file (used by the setup wizard). Returns false on
// error.
bool config_save_file(const ha_config *cfg, const char *path);

// Same, into a memory buffer. Returns the length written.
size_t config_write(const ha_config *cfg, char *out, size_t out_len);

// Returns true if the domain of entity_id is allowed by cfg->domains (or if
// the list is empty).
bool config_domain_allowed(const ha_config *cfg, const char *entity_id);

#endif // DSHA_CONFIG_H
