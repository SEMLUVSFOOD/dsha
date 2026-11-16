// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Home Assistant REST API client.
//
// https://developers.home-assistant.io/docs/api/rest/

#ifndef DSHA_HA_H
#define DSHA_HA_H

#include <stdbool.h>
#include <stdint.h>

#include "auth.h"
#include "config.h"
#include "entity.h"
#include "http.h"

// Layout from the DS Dashboard panel (Home Assistant integration)
#define LAYOUT_MAX_PAGES 16
#define LAYOUT_SLOTS 8

typedef struct {
    char name[24];
    int16_t slots[LAYOUT_SLOTS]; // Index in ha_client.entities, -1 = empty
} ha_page;

typedef struct {
    http_client http;
    char headers[600]; // "Authorization: Bearer ...\r\n"

    // Signed in with user name and password: access tokens are obtained from
    // the refresh token when needed (they expire after 30 minutes).
    char refresh_token[AUTH_TOKEN_LEN];
    bool signed_out; // The refresh token has been revoked

    ha_entity entities[ENTITY_MAX];
    int count;

    // In automatic mode: number of supported entities that didn't fit
    int skipped;
    bool auto_mode;
    bool demo; // Offline demo: no network, actions are simulated

    // Layout mode: the integration is installed and has a layout
    bool layout_mode;
    char layout_title[32];
    int num_pages;
    ha_page pages[LAYOUT_MAX_PAGES];

    char error[128]; // Last error message
    int last_status; // Last HTTP status
} ha_client;

void ha_init(ha_client *ha, const ha_config *cfg);

// Checks that the server is reachable and the token is valid (GET /api/).
bool ha_check(ha_client *ha);

// Loads the state of all entities. In automatic mode this builds the entity
// list from /api/states. In manual mode it fetches every configured entity.
// progress (optional) is called after every entity/chunk.
typedef void (*ha_progress_cb)(void *user, int done, int total);
bool ha_load_all(ha_client *ha, const ha_config *cfg, ha_progress_cb progress,
                 void *user);

// Refreshes the state of one entity
bool ha_refresh(ha_client *ha, ha_entity *e);

// Layout mode: refreshes all the entities of a page with one request
bool ha_refresh_page(ha_client *ha, int page);

// Parses the JSON of GET /api/dsha/layout and builds the pages and entity
// list. Exposed for the tests.
bool ha_parse_layout(ha_client *ha, const char *json);

// Runs an action on an entity (toggle, brightness...). The new state returned
// by Home Assistant is applied to the entity list.
bool ha_action(ha_client *ha, ha_entity *e, entity_action a);

// Parses a JSON document with states (a single state object, or an array of
// them) and applies it to the entity list. If add_new is true, unknown
// supported entities are appended. Exposed for the unit tests.
typedef struct ha_state_parser ha_state_parser;
ha_state_parser *ha_parser_begin(ha_client *ha, const ha_config *cfg,
                                 bool add_new);
bool ha_parser_feed(ha_state_parser *sp, const char *data, size_t len);
bool ha_parser_end(ha_state_parser *sp);

// Sorts the entities by domain and name (automatic mode)
void ha_sort(ha_client *ha);

#endif // DSHA_HA_H
