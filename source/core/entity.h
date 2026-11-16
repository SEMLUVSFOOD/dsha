// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// In-memory model of the Home Assistant entities shown by the app, and the
// knowledge of which actions are available for each domain.

#ifndef DSHA_ENTITY_H
#define DSHA_ENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ENTITY_MAX 128

#define ATTR_NONE (-1000000.0f) // Marker of "attribute not present"

// What tapping a tile does (chosen in the DS Dashboard panel)
#define TILE_ACTION_DEFAULT 0
#define TILE_ACTION_TRIGGER 1 // automation.trigger instead of toggle
#define TILE_ACTION_NONE    2 // Read only

typedef enum {
    DOM_OTHER = 0,
    DOM_LIGHT,
    DOM_SWITCH,
    DOM_INPUT_BOOLEAN,
    DOM_FAN,
    DOM_COVER,
    DOM_LOCK,
    DOM_CLIMATE,
    DOM_MEDIA_PLAYER,
    DOM_SCENE,
    DOM_SCRIPT,
    DOM_AUTOMATION,
    DOM_BUTTON,
    DOM_INPUT_BUTTON,
    DOM_VACUUM,
    DOM_SIREN,
    DOM_HUMIDIFIER,
    DOM_NUMBER,
    DOM_INPUT_NUMBER,
    DOM_SENSOR,
    DOM_BINARY_SENSOR,
    DOM_PERSON,
    DOM_WEATHER,
    DOM_COUNT
} entity_domain;

typedef struct {
    char id[64];
    char name[48];
    char state[40];
    char unit[16];
    char device_class[24];

    entity_domain domain;

    // Optional numeric attributes (ATTR_NONE if not present)
    float brightness;          // light: 0-255
    float current_temperature; // climate
    float target_temperature;  // climate
    float target_temp_step;    // climate
    float min_value, max_value, step; // number / input_number / climate
    float volume;              // media_player: 0-1
    float position;            // cover: 0-100
    char extra[48];            // media_player title, hvac_action, etc.

    // From the DS Dashboard panel (Home Assistant integration)
    char custom_name[48]; // Name chosen in the panel ("" = friendly name)
    uint8_t tile_action;  // TILE_ACTION_*

    bool dimmable;     // light: supports brightness
    bool loaded;       // Data received from the server at least once
    bool pending;      // An action has been sent, waiting for the new state
} ha_entity;

// Possible actions on an entity, mapped to buttons by the UI
typedef enum {
    ACT_PRIMARY,   // A button / tap: toggle, run, press...
    ACT_DECREASE,  // L: dim, lower temperature, volume down, close...
    ACT_INCREASE,  // R: brighten, raise temperature, volume up, open...
} entity_action;

entity_domain entity_domain_from_id(const char *entity_id);
const char *entity_domain_name(entity_domain d);
// Short human readable label for the domain ("Light", "Switch"...)
const char *entity_domain_label(entity_domain d);

void entity_init(ha_entity *e, const char *entity_id);

// Is the domain supported by the app at all (in automatic mode)?
bool entity_domain_supported(entity_domain d);

// Display order of the domains in automatic mode
int entity_domain_sort_key(entity_domain d);

bool entity_is_on(const ha_entity *e);
bool entity_is_unavailable(const ha_entity *e);
bool entity_has_action(const ha_entity *e, entity_action a);

// Short label of the action ("Toggle", "Dim -"...) for the button legend.
// Returns NULL if the action isn't available.
const char *entity_action_label(const ha_entity *e, entity_action a);

// Builds the Home Assistant service call for an action. Returns false if the
// action isn't available for this entity.
bool entity_build_service(const ha_entity *e, entity_action a,
                          char *domain, size_t domain_len,
                          char *service, size_t service_len,
                          char *json, size_t json_len);

// Human readable state, with units and translated values ("On", "21.5 °C"...)
void entity_format_state(const ha_entity *e, char *out, size_t len);

#endif // DSHA_ENTITY_H
