// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "entity.h"

typedef struct {
    const char *name;
    const char *label;
    bool supported; // Shown in automatic mode
    int sort;
} domain_info;

static const domain_info domains[DOM_COUNT] = {
    [DOM_OTHER]         = { "",              "Entity",   false, 99 },
    [DOM_LIGHT]         = { "light",         "Light",    true,  0 },
    [DOM_SWITCH]        = { "switch",        "Switch",   true,  1 },
    [DOM_INPUT_BOOLEAN] = { "input_boolean", "Toggle",   true,  2 },
    [DOM_FAN]           = { "fan",           "Fan",      true,  3 },
    [DOM_COVER]         = { "cover",         "Cover",    true,  4 },
    [DOM_LOCK]          = { "lock",          "Lock",     true,  5 },
    [DOM_CLIMATE]       = { "climate",       "Climate",  true,  6 },
    [DOM_MEDIA_PLAYER]  = { "media_player",  "Media",    true,  7 },
    [DOM_VACUUM]        = { "vacuum",        "Vacuum",   true,  8 },
    [DOM_HUMIDIFIER]    = { "humidifier",    "Humidifier", true, 9 },
    [DOM_SIREN]         = { "siren",         "Siren",    true,  10 },
    [DOM_SCENE]         = { "scene",         "Scene",    true,  11 },
    [DOM_SCRIPT]        = { "script",        "Script",   true,  12 },
    [DOM_BUTTON]        = { "button",        "Button",   true,  13 },
    [DOM_INPUT_BUTTON]  = { "input_button",  "Button",   true,  14 },
    [DOM_NUMBER]        = { "number",        "Number",   true,  15 },
    [DOM_INPUT_NUMBER]  = { "input_number",  "Number",   true,  16 },
    [DOM_AUTOMATION]    = { "automation",    "Automation", true, 17 },
    [DOM_WEATHER]       = { "weather",       "Weather",  true,  18 },
    [DOM_PERSON]        = { "person",        "Person",   true,  19 },
    [DOM_BINARY_SENSOR] = { "binary_sensor", "Binary sensor", true, 20 },
    [DOM_SENSOR]        = { "sensor",        "Sensor",   true,  21 },
};

entity_domain entity_domain_from_id(const char *entity_id)
{
    const char *dot = strchr(entity_id, '.');
    if (dot == NULL)
        return DOM_OTHER;
    size_t len = (size_t)(dot - entity_id);

    for (int i = 1; i < DOM_COUNT; i++)
    {
        if (strlen(domains[i].name) == len
            && strncmp(domains[i].name, entity_id, len) == 0)
            return (entity_domain)i;
    }
    return DOM_OTHER;
}

const char *entity_domain_name(entity_domain d)
{
    return domains[d].name;
}

const char *entity_domain_label(entity_domain d)
{
    return domains[d].label;
}

bool entity_domain_supported(entity_domain d)
{
    return domains[d].supported;
}

int entity_domain_sort_key(entity_domain d)
{
    return domains[d].sort;
}

void entity_init(ha_entity *e, const char *entity_id)
{
    memset(e, 0, sizeof(*e));
    snprintf(e->id, sizeof(e->id), "%s", entity_id);
    e->domain = entity_domain_from_id(entity_id);

    // Until the real name is known, show the object id ("living_room")
    const char *dot = strchr(entity_id, '.');
    snprintf(e->name, sizeof(e->name), "%s", dot ? dot + 1 : entity_id);
    for (char *p = e->name; *p; p++)
    {
        if (*p == '_')
            *p = ' ';
    }
    e->name[0] = (char)toupper((unsigned char)e->name[0]);

    snprintf(e->state, sizeof(e->state), "unknown");
    e->brightness = ATTR_NONE;
    e->current_temperature = ATTR_NONE;
    e->target_temperature = ATTR_NONE;
    e->target_temp_step = ATTR_NONE;
    e->min_value = ATTR_NONE;
    e->max_value = ATTR_NONE;
    e->step = ATTR_NONE;
    e->volume = ATTR_NONE;
    e->position = ATTR_NONE;
}

static bool state_is(const ha_entity *e, const char *s)
{
    return strcmp(e->state, s) == 0;
}

bool entity_is_unavailable(const ha_entity *e)
{
    return state_is(e, "unavailable");
}

bool entity_is_on(const ha_entity *e)
{
    switch (e->domain)
    {
        case DOM_COVER:
            return state_is(e, "open") || state_is(e, "opening");
        case DOM_LOCK:
            return state_is(e, "unlocked") || state_is(e, "open")
                   || state_is(e, "unlocking") || state_is(e, "opening");
        case DOM_CLIMATE:
            return !state_is(e, "off") && !state_is(e, "unavailable")
                   && !state_is(e, "unknown");
        case DOM_MEDIA_PLAYER:
            return state_is(e, "playing") || state_is(e, "on")
                   || state_is(e, "paused") || state_is(e, "buffering");
        case DOM_VACUUM:
            return state_is(e, "cleaning") || state_is(e, "returning");
        case DOM_PERSON:
            return state_is(e, "home");
        case DOM_SCRIPT:
            return state_is(e, "on");
        default:
            return state_is(e, "on") || state_is(e, "home")
                   || state_is(e, "open");
    }
}

static bool has_number_range(const ha_entity *e)
{
    return e->min_value != ATTR_NONE && e->max_value != ATTR_NONE;
}

const char *entity_action_label(const ha_entity *e, entity_action a)
{
    if (entity_is_unavailable(e))
        return NULL;

    switch (e->domain)
    {
        case DOM_LIGHT:
            if (a == ACT_PRIMARY)
                return "Toggle";
            if (!e->dimmable)
                return NULL;
            return a == ACT_DECREASE ? "Dim" : "Brighter";

        case DOM_SWITCH:
        case DOM_INPUT_BOOLEAN:
        case DOM_FAN:
        case DOM_AUTOMATION:
        case DOM_SIREN:
        case DOM_HUMIDIFIER:
            return a == ACT_PRIMARY ? "Toggle" : NULL;

        case DOM_COVER:
            if (a == ACT_PRIMARY)
                return "Toggle";
            return a == ACT_DECREASE ? "Close" : "Open";

        case DOM_LOCK:
            if (a != ACT_PRIMARY)
                return NULL;
            return state_is(e, "locked") ? "Unlock" : "Lock";

        case DOM_CLIMATE:
            if (a == ACT_PRIMARY)
                return "On/Off";
            if (e->target_temperature == ATTR_NONE)
                return NULL;
            return a == ACT_DECREASE ? "Temp -" : "Temp +";

        case DOM_MEDIA_PLAYER:
            if (a == ACT_PRIMARY)
                return "Play/Pause";
            return a == ACT_DECREASE ? "Vol -" : "Vol +";

        case DOM_SCENE:
        case DOM_SCRIPT:
            return a == ACT_PRIMARY ? "Run" : NULL;

        case DOM_BUTTON:
        case DOM_INPUT_BUTTON:
            return a == ACT_PRIMARY ? "Press" : NULL;

        case DOM_VACUUM:
            if (a != ACT_PRIMARY)
                return NULL;
            return entity_is_on(e) ? "Go home" : "Start";

        case DOM_NUMBER:
        case DOM_INPUT_NUMBER:
            if (a == ACT_PRIMARY || !has_number_range(e))
                return NULL;
            return a == ACT_DECREASE ? "Value -" : "Value +";

        default:
            return NULL;
    }
}

bool entity_has_action(const ha_entity *e, entity_action a)
{
    return entity_action_label(e, a) != NULL;
}

// Prints a float without trailing zeros ("21", "21.5", "0.25")
static void format_number(char *out, size_t len, float v)
{
    snprintf(out, len, "%.2f", (double)v);
    char *dot = strchr(out, '.');
    if (dot)
    {
        char *end = out + strlen(out) - 1;
        while (end > dot && *end == '0')
            *end-- = '\0';
        if (end == dot)
            *end = '\0';
    }
}

bool entity_build_service(const ha_entity *e, entity_action a,
                          char *domain, size_t domain_len,
                          char *service, size_t service_len,
                          char *json, size_t json_len)
{
    if (!entity_has_action(e, a))
        return false;

    const char *dom = entity_domain_name(e->domain);
    const char *svc = NULL;
    char extra[64] = "";
    char num[24];

    switch (e->domain)
    {
        case DOM_LIGHT:
            if (a == ACT_PRIMARY)
                svc = "toggle";
            else
            {
                svc = "turn_on";
                snprintf(extra, sizeof(extra), ",\"brightness_step_pct\":%d",
                         a == ACT_DECREASE ? -10 : 10);
            }
            break;

        case DOM_SWITCH:
        case DOM_INPUT_BOOLEAN:
        case DOM_FAN:
        case DOM_AUTOMATION:
        case DOM_SIREN:
        case DOM_HUMIDIFIER:
            svc = "toggle";
            break;

        case DOM_COVER:
            svc = a == ACT_PRIMARY ? "toggle"
                  : a == ACT_DECREASE ? "close_cover" : "open_cover";
            break;

        case DOM_LOCK:
            svc = state_is(e, "locked") ? "unlock" : "lock";
            break;

        case DOM_CLIMATE:
            if (a == ACT_PRIMARY)
                svc = "toggle";
            else
            {
                float step = e->target_temp_step != ATTR_NONE
                                 ? e->target_temp_step : 0.5f;
                float t = e->target_temperature
                          + (a == ACT_DECREASE ? -step : step);
                if (e->min_value != ATTR_NONE && t < e->min_value)
                    t = e->min_value;
                if (e->max_value != ATTR_NONE && t > e->max_value)
                    t = e->max_value;
                format_number(num, sizeof(num), t);
                svc = "set_temperature";
                snprintf(extra, sizeof(extra), ",\"temperature\":%s", num);
            }
            break;

        case DOM_MEDIA_PLAYER:
            svc = a == ACT_PRIMARY ? "media_play_pause"
                  : a == ACT_DECREASE ? "volume_down" : "volume_up";
            break;

        case DOM_SCENE:
        case DOM_SCRIPT:
            svc = "turn_on";
            break;

        case DOM_BUTTON:
        case DOM_INPUT_BUTTON:
            svc = "press";
            break;

        case DOM_VACUUM:
            svc = entity_is_on(e) ? "return_to_base" : "start";
            break;

        case DOM_NUMBER:
        case DOM_INPUT_NUMBER:
        {
            float step = e->step != ATTR_NONE && e->step > 0 ? e->step : 1.0f;
            float v = (float)atof(e->state);
            v += a == ACT_DECREASE ? -step : step;
            if (v < e->min_value)
                v = e->min_value;
            if (v > e->max_value)
                v = e->max_value;
            format_number(num, sizeof(num), v);
            svc = "set_value";
            snprintf(extra, sizeof(extra), ",\"value\":%s", num);
            break;
        }

        default:
            return false;
    }

    snprintf(domain, domain_len, "%s", dom);
    snprintf(service, service_len, "%s", svc);
    snprintf(json, json_len, "{\"entity_id\":\"%s\"%s}", e->id, extra);
    return true;
}

// "partlycloudy" -> "Partly cloudy", "not_home" -> "Not home"
static void humanize(char *out, size_t len, const char *s)
{
    static const struct { const char *from, *to; } map[] = {
        { "on", "On" }, { "off", "Off" },
        { "unavailable", "Unavailable" }, { "unknown", "Unknown" },
        { "not_home", "Away" }, { "home", "Home" },
        { "partlycloudy", "Partly cloudy" }, { "clear-night", "Clear night" },
        { "lightning-rainy", "Storm & rain" }, { "snowy-rainy", "Sleet" },
        { "heat_cool", "Heat/Cool" }, { "fan_only", "Fan only" },
    };

    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
    {
        if (strcmp(s, map[i].from) == 0)
        {
            snprintf(out, len, "%s", map[i].to);
            return;
        }
    }

    snprintf(out, len, "%s", s);
    for (char *p = out; *p; p++)
    {
        if (*p == '_' || *p == '-')
            *p = ' ';
    }
    out[0] = (char)toupper((unsigned char)out[0]);
}

static bool is_numeric(const char *s)
{
    if (*s == '-' || *s == '+')
        s++;
    bool digits = false;
    bool dot = false;
    for (; *s; s++)
    {
        if (isdigit((unsigned char)*s))
            digits = true;
        else if (*s == '.' && !dot)
            dot = true;
        else
            return false;
    }
    return digits;
}

static bool device_class_in(const ha_entity *e, const char *const *list)
{
    for (; *list; list++)
    {
        if (strcmp(e->device_class, *list) == 0)
            return true;
    }
    return false;
}

void entity_format_state(const ha_entity *e, char *out, size_t len)
{
    char num[48];

    if (state_is(e, "unavailable") || state_is(e, "unknown"))
    {
        humanize(out, len, e->state);
        return;
    }

    switch (e->domain)
    {
        case DOM_LIGHT:
            if (entity_is_on(e) && e->brightness != ATTR_NONE)
            {
                int pct = (int)((e->brightness * 100.0f + 127.0f) / 255.0f);
                snprintf(out, len, "On %d%%", pct);
                return;
            }
            break;

        case DOM_COVER:
            if (e->position != ATTR_NONE && !state_is(e, "closed"))
            {
                humanize(num, sizeof(num), e->state);
                snprintf(out, len, "%s %d%%", num, (int)e->position);
                return;
            }
            break;

        case DOM_CLIMATE:
            if (e->current_temperature != ATTR_NONE)
            {
                char mode[24];
                humanize(mode, sizeof(mode), e->state);
                format_number(num, sizeof(num), e->current_temperature);
                snprintf(out, len, "%s %s\xC2\xB0", mode, num);
                return;
            }
            break;

        case DOM_WEATHER:
            if (e->target_temperature != ATTR_NONE)
            {
                char cond[24];
                humanize(cond, sizeof(cond), e->state);
                format_number(num, sizeof(num), e->target_temperature);
                snprintf(out, len, "%s %s%s", cond, num, e->unit);
                return;
            }
            break;

        case DOM_SCENE:
            snprintf(out, len, "Activate");
            return;

        case DOM_SCRIPT:
            snprintf(out, len, "%s", entity_is_on(e) ? "Running" : "Ready");
            return;

        case DOM_BUTTON:
        case DOM_INPUT_BUTTON:
            snprintf(out, len, "Press");
            return;

        case DOM_BINARY_SENSOR:
        {
            static const char *const openings[] = {
                "door", "window", "garage_door", "opening", NULL };
            static const char *const motion[] = {
                "motion", "occupancy", "presence", "moving", "sound",
                "vibration", NULL };
            static const char *const problems[] = {
                "problem", "safety", "smoke", "gas", "carbon_monoxide",
                "tamper", NULL };
            bool on = state_is(e, "on");

            if (device_class_in(e, openings))
                snprintf(out, len, "%s", on ? "Open" : "Closed");
            else if (device_class_in(e, motion))
                snprintf(out, len, "%s", on ? "Detected" : "Clear");
            else if (device_class_in(e, problems))
                snprintf(out, len, "%s", on ? "Alert!" : "OK");
            else if (strcmp(e->device_class, "moisture") == 0)
                snprintf(out, len, "%s", on ? "Wet" : "Dry");
            else if (strcmp(e->device_class, "battery") == 0)
                snprintf(out, len, "%s", on ? "Low" : "Normal");
            else if (strcmp(e->device_class, "connectivity") == 0)
                snprintf(out, len, "%s", on ? "Connected" : "Disconnected");
            else
                break;
            return;
        }

        default:
            break;
    }

    if (is_numeric(e->state))
    {
        // Sensors often report far too many decimals
        const char *dot = strchr(e->state, '.');
        if (dot && strlen(dot + 1) > 2)
            format_number(num, sizeof(num), (float)atof(e->state));
        else
            snprintf(num, sizeof(num), "%s", e->state);

        if (e->unit[0] != '\0')
        {
            // No space before "%" or "°C"-like units, like HA does
            bool tight = e->unit[0] == '%';
            snprintf(out, len, "%s%s%s", num, tight ? "" : " ", e->unit);
        }
        else
        {
            snprintf(out, len, "%s", num);
        }
        return;
    }

    // ISO timestamps (e.g. sensors with device_class timestamp): show date+time
    if (strlen(e->state) >= 19 && e->state[4] == '-' && e->state[10] == 'T')
    {
        snprintf(out, len, "%.10s %.5s", e->state, e->state + 11);
        return;
    }

    humanize(out, len, e->state);
    if (e->unit[0] != '\0' && strlen(out) + strlen(e->unit) + 2 < len)
    {
        strcat(out, " ");
        strcat(out, e->unit);
    }
}
