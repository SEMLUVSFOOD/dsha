// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "demo_data.h"
#include "ha.h"
#include "json_stream.h"
#include "validate.h"

struct ha_state_parser {
    ha_client *ha;
    const ha_config *cfg;
    bool add_new;

    json_parser json;
    int base;          // Depth of the state objects (0 or 1), -1 = unknown
    bool in_entity;
    bool in_attrs;
    bool in_color_modes;
    char key1[32];     // Key inside the state object
    char key2[32];     // Key inside "attributes"

    ha_entity cur;
    bool got_name;
    int entities_seen;
};

static ha_state_parser state_parser;

void ha_init(ha_client *ha, const ha_config *cfg)
{
    memset(ha, 0, sizeof(*ha));
    http_client_init_url(&ha->http, cfg->host, cfg->port, cfg->https);
    ha->http.allow_public = cfg->allow_public_server;
    snprintf(ha->headers, sizeof(ha->headers),
             "Authorization: Bearer %s\r\n", cfg->token);
    if (cfg->token[0] == '\0')
    {
        // Get an access token with the first request
        snprintf(ha->refresh_token, sizeof(ha->refresh_token), "%s",
                 cfg->refresh_token);
        ha->headers[0] = '\0';
    }

    ha->demo = cfg->demo;
    ha->auto_mode = cfg->num_entities == 0;
    if (!ha->auto_mode)
    {
        for (int i = 0; i < cfg->num_entities && i < ENTITY_MAX; i++)
            entity_init(&ha->entities[i], cfg->entities[i]);
        ha->count = cfg->num_entities < ENTITY_MAX ? cfg->num_entities
                                                   : ENTITY_MAX;
    }
}

static ha_entity *find_entity(ha_client *ha, const char *id)
{
    for (int i = 0; i < ha->count; i++)
    {
        if (strcmp(ha->entities[i].id, id) == 0)
            return &ha->entities[i];
    }
    return NULL;
}

static float to_float(json_event ev, const char *value)
{
    if (ev == JSON_EV_NUMBER)
        return (float)atof(value);
    if (ev == JSON_EV_STRING && value[0] != '\0'
        && (isdigit((unsigned char)value[0]) || value[0] == '-'))
        return (float)atof(value);
    return ATTR_NONE;
}

static void set_str(char *dst, size_t size, const char *src)
{
    snprintf(dst, size, "%s", src);
}

static void handle_attribute(ha_state_parser *sp, json_event ev,
                             const char *value)
{
    ha_entity *e = &sp->cur;
    const char *k = sp->key2;

    if (strcmp(k, "friendly_name") == 0 && ev == JSON_EV_STRING)
    {
        set_str(e->name, sizeof(e->name), value);
        sp->got_name = true;
    }
    else if (strcmp(k, "unit_of_measurement") == 0 && ev == JSON_EV_STRING)
        set_str(e->unit, sizeof(e->unit), value);
    else if (strcmp(k, "temperature_unit") == 0 && ev == JSON_EV_STRING)
        set_str(e->unit, sizeof(e->unit), value); // weather
    else if (strcmp(k, "device_class") == 0 && ev == JSON_EV_STRING)
        set_str(e->device_class, sizeof(e->device_class), value);
    else if (strcmp(k, "brightness") == 0)
        e->brightness = to_float(ev, value);
    else if (strcmp(k, "current_temperature") == 0)
        e->current_temperature = to_float(ev, value);
    else if (strcmp(k, "temperature") == 0)
        e->target_temperature = to_float(ev, value);
    else if (strcmp(k, "target_temp_step") == 0)
        e->target_temp_step = to_float(ev, value);
    else if (strcmp(k, "min") == 0 || strcmp(k, "min_temp") == 0)
        e->min_value = to_float(ev, value);
    else if (strcmp(k, "max") == 0 || strcmp(k, "max_temp") == 0)
        e->max_value = to_float(ev, value);
    else if (strcmp(k, "step") == 0)
        e->step = to_float(ev, value);
    else if (strcmp(k, "volume_level") == 0)
        e->volume = to_float(ev, value);
    else if (strcmp(k, "current_position") == 0)
        e->position = to_float(ev, value);
    else if ((strcmp(k, "media_title") == 0 || strcmp(k, "hvac_action") == 0)
             && ev == JSON_EV_STRING)
        set_str(e->extra, sizeof(e->extra), value);
}

static void commit_entity(ha_state_parser *sp)
{
    ha_client *ha = sp->ha;
    ha_entity *cur = &sp->cur;

    if (!valid_entity_id(cur->id))
        return;

    sp->entities_seen++;

    // Keep the friendly name from the id if there isn't any
    cur->domain = entity_domain_from_id(cur->id);
    if (!sp->got_name)
    {
        ha_entity tmp;
        entity_init(&tmp, cur->id);
        set_str(cur->name, sizeof(cur->name), tmp.name);
    }
    cur->loaded = true;
    cur->pending = false;

    ha_entity *dst = find_entity(ha, cur->id);
    if (dst == NULL)
    {
        if (!sp->add_new)
            return;
        if (!entity_domain_supported(cur->domain))
            return;
        if (sp->cfg != NULL && !config_domain_allowed(sp->cfg, cur->id))
            return;
        if (ha->count >= ENTITY_MAX)
        {
            ha->skipped++;
            return;
        }
        dst = &ha->entities[ha->count++];
    }

    // Keep the settings from the DS Dashboard panel
    char custom_name[sizeof(dst->custom_name)];
    snprintf(custom_name, sizeof(custom_name), "%s", dst->custom_name);
    uint8_t tile_action = dst->tile_action;

    *dst = *cur;

    snprintf(dst->custom_name, sizeof(dst->custom_name), "%s", custom_name);
    dst->tile_action = tile_action;
    if (custom_name[0] != '\0')
        set_str(dst->name, sizeof(dst->name), custom_name);
}

static void begin_entity(ha_state_parser *sp)
{
    entity_init(&sp->cur, "");
    sp->cur.id[0] = '\0';
    sp->got_name = false;
    sp->in_entity = true;
    sp->in_attrs = false;
    sp->in_color_modes = false;
    sp->key1[0] = '\0';
    sp->key2[0] = '\0';
}

static void json_cb(void *user, json_event ev, const char *value, int depth)
{
    ha_state_parser *sp = user;

    // Find out if the document is an array of states or a single state
    if (sp->base < 0)
    {
        if (ev == JSON_EV_ARRAY_START && depth == 0)
        {
            sp->base = 1;
            return;
        }
        if (ev == JSON_EV_OBJECT_START && depth == 0)
            sp->base = 0;
        else
            return;
    }

    const int b = sp->base;

    if (!sp->in_entity)
    {
        if (ev == JSON_EV_OBJECT_START && depth == b)
            begin_entity(sp);
        return;
    }

    switch (ev)
    {
        case JSON_EV_OBJECT_END:
            if (depth == b)
            {
                commit_entity(sp);
                sp->in_entity = false;
            }
            else if (depth == b + 1)
            {
                sp->in_attrs = false;
            }
            return;

        case JSON_EV_OBJECT_START:
            if (depth == b + 1 && strcmp(sp->key1, "attributes") == 0)
                sp->in_attrs = true;
            return;

        case JSON_EV_ARRAY_START:
            if (sp->in_attrs && depth == b + 2
                && strcmp(sp->key2, "supported_color_modes") == 0)
                sp->in_color_modes = true;
            return;

        case JSON_EV_ARRAY_END:
            if (depth == b + 2)
                sp->in_color_modes = false;
            return;

        case JSON_EV_KEY:
            if (depth == b + 1)
                set_str(sp->key1, sizeof(sp->key1), value);
            else if (depth == b + 2 && sp->in_attrs)
                set_str(sp->key2, sizeof(sp->key2), value);
            return;

        default:
            break;
    }

    // Values
    if (depth == b + 1)
    {
        if (strcmp(sp->key1, "entity_id") == 0 && ev == JSON_EV_STRING)
        {
            set_str(sp->cur.id, sizeof(sp->cur.id), value);
            sp->cur.domain = entity_domain_from_id(value);
        }
        else if (strcmp(sp->key1, "state") == 0)
        {
            set_str(sp->cur.state, sizeof(sp->cur.state),
                    ev == JSON_EV_NULL ? "unknown" : value);
        }
    }
    else if (depth == b + 2 && sp->in_attrs)
    {
        handle_attribute(sp, ev, value);
    }
    else if (depth == b + 3 && sp->in_color_modes && ev == JSON_EV_STRING)
    {
        if (strcmp(value, "onoff") != 0 && strcmp(value, "unknown") != 0)
            sp->cur.dimmable = true;
    }
}

ha_state_parser *ha_parser_begin(ha_client *ha, const ha_config *cfg,
                                 bool add_new)
{
    ha_state_parser *sp = &state_parser;
    memset(sp, 0, sizeof(*sp));
    sp->ha = ha;
    sp->cfg = cfg;
    sp->add_new = add_new;
    sp->base = -1;
    json_init(&sp->json, json_cb, sp);
    return sp;
}

bool ha_parser_feed(ha_state_parser *sp, const char *data, size_t len)
{
    return json_feed(&sp->json, data, len);
}

bool ha_parser_end(ha_state_parser *sp)
{
    return json_finish(&sp->json);
}

// HTTP helpers
// ------------

// Gets a new access token using the refresh token
static bool renew_access_token(ha_client *ha)
{
    static char access[AUTH_TOKEN_LEN];
    bool revoked = false;

    if (!auth_refresh(&ha->http, ha->refresh_token, access, sizeof(access),
                      &revoked, ha->error, sizeof(ha->error)))
    {
        ha->signed_out = revoked;
        return false;
    }
    snprintf(ha->headers, sizeof(ha->headers),
             "Authorization: Bearer %s\r\n", access);
    return true;
}

// Sends an authenticated request. When signed in with a refresh token, an
// expired access token is renewed and the request is repeated.
static bool ha_http(ha_client *ha, const char *method, const char *path,
                    const char *body, http_data_cb cb, void *user,
                    http_result *res)
{
    if (ha->refresh_token[0] != '\0' && ha->headers[0] == '\0')
    {
        if (!renew_access_token(ha))
        {
            if (ha->signed_out)
            {
                res->status = 401; // Reported as "signed out"
                return true;
            }
            res->status = 0;
            snprintf(res->error, sizeof(res->error), "%s", ha->error);
            return false;
        }
    }

    bool ok = http_request(&ha->http, method, path, ha->headers, body, cb,
                           user, res);

    if (ok && res->status == 401 && ha->refresh_token[0] != '\0')
    {
        if (!renew_access_token(ha))
        {
            if (!ha->signed_out)
            {
                res->status = 0;
                snprintf(res->error, sizeof(res->error), "%s", ha->error);
                return false;
            }
            return true; // Report the 401
        }
        ok = http_request(&ha->http, method, path, ha->headers, body, cb,
                          user, res);
    }
    return ok;
}

typedef struct {
    ha_state_parser *sp;
    ha_progress_cb progress;
    void *user;
    int last_seen;
} fetch_ctx;

static bool fetch_data_cb(void *user, const char *data, size_t len)
{
    fetch_ctx *ctx = user;
    if (!ha_parser_feed(ctx->sp, data, len))
        return false;

    if (ctx->progress && ctx->sp->entities_seen != ctx->last_seen)
    {
        ctx->last_seen = ctx->sp->entities_seen;
        ctx->progress(ctx->user, ctx->sp->entities_seen, -1);
    }
    return true;
}

static bool status_ok(ha_client *ha, const http_result *res)
{
    ha->last_status = res->status;

    if (res->status == 0)
    {
        set_str(ha->error, sizeof(ha->error), res->error);
        return false;
    }
    if (res->status == 401 || res->status == 403)
    {
        if (ha->signed_out)
            snprintf(ha->error, sizeof(ha->error),
                     "Signed out by Home Assistant. Please sign in again.");
        else if (ha->refresh_token[0] != '\0')
            snprintf(ha->error, sizeof(ha->error),
                     "Access denied (HTTP %d).", res->status);
        else
            snprintf(ha->error, sizeof(ha->error),
                     "Access denied (HTTP %d). Check the token.", res->status);
        return false;
    }
    if (res->status < 200 || res->status >= 300)
    {
        snprintf(ha->error, sizeof(ha->error), "Server error: HTTP %d",
                 res->status);
        return false;
    }
    ha->error[0] = '\0';
    return true;
}

static bool ignore_body(void *user, const char *data, size_t len)
{
    (void)user;
    (void)data;
    (void)len;
    return true;
}

bool ha_check(ha_client *ha)
{
    if (ha->demo)
        return true;

    http_result res;
    if (!ha_http(ha, "GET", "/api/", NULL, ignore_body, NULL, &res))
    {
        ha->last_status = 0;
        set_str(ha->error, sizeof(ha->error), res.error);
        return false;
    }
    if (res.status == 404)
    {
        snprintf(ha->error, sizeof(ha->error),
                 "HTTP 404: is this a Home Assistant server?");
        return false;
    }
    return status_ok(ha, &res);
}

static bool fetch_states(ha_client *ha, const ha_config *cfg, const char *path,
                         bool add_new, ha_progress_cb progress, void *user)
{
    fetch_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.sp = ha_parser_begin(ha, cfg, add_new);
    ctx.progress = progress;
    ctx.user = user;

    http_result res;
    bool ok = ha_http(ha, "GET", path, NULL, fetch_data_cb, &ctx, &res);
    if (!ok)
    {
        ha->last_status = 0;
        if (ctx.sp->json.error)
            set_str(ha->error, sizeof(ha->error), "Invalid JSON from server");
        else
            set_str(ha->error, sizeof(ha->error), res.error);
        return false;
    }

    if (!status_ok(ha, &res))
        return false;

    if (!ha_parser_end(ctx.sp))
    {
        set_str(ha->error, sizeof(ha->error), "Invalid JSON from server");
        return false;
    }
    return true;
}

static int compare_entities(const void *a, const void *b)
{
    const ha_entity *ea = a;
    const ha_entity *eb = b;
    int da = entity_domain_sort_key(ea->domain);
    int db = entity_domain_sort_key(eb->domain);
    if (da != db)
        return da - db;
    return strcasecmp(ea->name, eb->name);
}

void ha_sort(ha_client *ha)
{
    qsort(ha->entities, (size_t)ha->count, sizeof(ha_entity),
          compare_entities);
}

// Layout of the DS Dashboard panel
// --------------------------------

typedef struct {
    ha_client *ha;
    json_parser json;
    bool started;
    bool in_pages, in_tiles, in_tile;
    char key1[16], key3[16], key5[16];
    int slot;
    int tiles;
    char tile_id[64], tile_name[48], tile_action[12];
} layout_parser;

static layout_parser lp;

static void layout_reset(ha_client *ha)
{
    ha->count = 0;
    ha->skipped = 0;
    ha->num_pages = 0;
    ha->layout_title[0] = '\0';
}

static void layout_add_tile(layout_parser *p)
{
    ha_client *ha = p->ha;
    if (ha->num_pages == 0 || p->slot >= LAYOUT_SLOTS
        || !valid_entity_id(p->tile_id))
        return;

    ha_entity *e = find_entity(ha, p->tile_id);
    if (e == NULL)
    {
        if (ha->count >= ENTITY_MAX)
        {
            ha->skipped++;
            return;
        }
        e = &ha->entities[ha->count++];
        entity_init(e, p->tile_id);
        if (p->tile_name[0] != '\0')
        {
            set_str(e->custom_name, sizeof(e->custom_name), p->tile_name);
            set_str(e->name, sizeof(e->name), p->tile_name);
        }
        if (strcmp(p->tile_action, "trigger") == 0)
            e->tile_action = TILE_ACTION_TRIGGER;
        else if (strcmp(p->tile_action, "none") == 0)
            e->tile_action = TILE_ACTION_NONE;
    }

    ha->pages[ha->num_pages - 1].slots[p->slot] = (int16_t)(e - ha->entities);
    p->tiles++;
}

static void layout_cb(void *user, json_event ev, const char *value, int depth)
{
    layout_parser *p = user;
    ha_client *ha = p->ha;

    switch (ev)
    {
        case JSON_EV_KEY:
            if (depth == 1)
                set_str(p->key1, sizeof(p->key1), value);
            else if (depth == 3)
                set_str(p->key3, sizeof(p->key3), value);
            else if (depth == 5)
                set_str(p->key5, sizeof(p->key5), value);
            return;

        case JSON_EV_ARRAY_START:
            if (depth == 1 && strcmp(p->key1, "pages") == 0)
                p->in_pages = true;
            else if (depth == 3 && p->in_pages && strcmp(p->key3, "tiles") == 0)
            {
                p->in_tiles = true;
                p->slot = 0;
            }
            return;

        case JSON_EV_ARRAY_END:
            if (depth == 1)
                p->in_pages = false;
            else if (depth == 3)
                p->in_tiles = false;
            return;

        case JSON_EV_OBJECT_START:
            if (depth == 2 && p->in_pages)
            {
                if (ha->num_pages < LAYOUT_MAX_PAGES)
                {
                    ha_page *page = &ha->pages[ha->num_pages++];
                    snprintf(page->name, sizeof(page->name), "Page %d",
                             ha->num_pages);
                    for (int i = 0; i < LAYOUT_SLOTS; i++)
                        page->slots[i] = -1;
                }
                else
                {
                    p->in_pages = false; // Ignore extra pages
                }
            }
            else if (depth == 4 && p->in_tiles)
            {
                p->in_tile = true;
                p->tile_id[0] = p->tile_name[0] = p->tile_action[0] = '\0';
            }
            return;

        case JSON_EV_OBJECT_END:
            if (depth == 4 && p->in_tile)
            {
                layout_add_tile(p);
                p->in_tile = false;
                p->slot++;
            }
            return;

        default:
            break;
    }

    // Values
    if (depth == 1 && strcmp(p->key1, "title") == 0 && ev == JSON_EV_STRING)
        set_str(ha->layout_title, sizeof(ha->layout_title), value);
    else if (depth == 3 && p->in_pages && strcmp(p->key3, "name") == 0
             && ev == JSON_EV_STRING && ha->num_pages > 0)
        set_str(ha->pages[ha->num_pages - 1].name,
                sizeof(ha->pages[0].name), value);
    else if (depth == 4 && p->in_tiles && ev == JSON_EV_NULL)
        p->slot++; // Empty slot
    else if (depth == 5 && p->in_tile && ev == JSON_EV_STRING)
    {
        if (strcmp(p->key5, "entity_id") == 0)
            set_str(p->tile_id, sizeof(p->tile_id), value);
        else if (strcmp(p->key5, "name") == 0)
            set_str(p->tile_name, sizeof(p->tile_name), value);
        else if (strcmp(p->key5, "action") == 0)
            set_str(p->tile_action, sizeof(p->tile_action), value);
    }
}

static void layout_begin(ha_client *ha)
{
    memset(&lp, 0, sizeof(lp));
    lp.ha = ha;
    json_init(&lp.json, layout_cb, &lp);
}

static bool layout_data_cb(void *user, const char *data, size_t len)
{
    layout_parser *p = user;
    if (!p->started)
    {
        // Only reset the entities when a layout is really received
        layout_reset(p->ha);
        p->started = true;
    }
    return json_feed(&p->json, data, len);
}

bool ha_parse_layout(ha_client *ha, const char *json)
{
    layout_begin(ha);
    return layout_data_cb(&lp, json, strlen(json)) && json_finish(&lp.json)
           && lp.tiles > 0;
}

// Puts back the configured entities after a failed or empty layout
static void restore_entities(ha_client *ha, const ha_config *cfg)
{
    ha->layout_mode = false;
    ha->num_pages = 0;
    ha->count = 0;
    ha->layout_title[0] = '\0';
    if (cfg != NULL && !ha->auto_mode)
    {
        for (int i = 0; i < cfg->num_entities && i < ENTITY_MAX; i++)
            entity_init(&ha->entities[i], cfg->entities[i]);
        ha->count = cfg->num_entities < ENTITY_MAX ? cfg->num_entities
                                                   : ENTITY_MAX;
    }
}

// Returns 1 if a layout has been loaded, 0 if there is no layout (the
// integration isn't installed, or the layout is empty) and -1 on error.
static int load_layout(ha_client *ha, const ha_config *cfg)
{
    layout_begin(ha);

    http_result res;
    if (!ha_http(ha, "GET", "/api/dsha/layout", NULL, layout_data_cb, &lp,
                 &res))
    {
        ha->last_status = 0;
        set_str(ha->error, sizeof(ha->error), res.error);
        if (lp.started)
            restore_entities(ha, cfg);
        return -1;
    }
    if (res.status == 404)
        return 0; // The integration isn't installed: normal mode
    if (!status_ok(ha, &res))
        return -1;

    if (!json_finish(&lp.json) || lp.tiles == 0)
    {
        // Broken or empty layout (nothing arranged in the panel yet)
        restore_entities(ha, cfg);
        return 0;
    }
    ha->layout_mode = true;
    return 1;
}

bool ha_refresh_page(ha_client *ha, int page)
{
    char path[48];
    if (ha->demo)
        return true;
    snprintf(path, sizeof(path), "/api/dsha/states?page=%d", page);
    return fetch_states(ha, NULL, path, false, NULL, NULL);
}

static bool demo_load(ha_client *ha, const ha_config *cfg)
{
    // The demo shows a layout like the one made in the DS Dashboard panel
    if (cfg != NULL && cfg->use_layout && ha_parse_layout(ha, demo_layout_json))
    {
        ha->layout_mode = true;
        ha_state_parser *sp = ha_parser_begin(ha, NULL, false);
        if (!ha_parser_feed(sp, demo_states_json, strlen(demo_states_json))
            || !ha_parser_end(sp))
        {
            set_str(ha->error, sizeof(ha->error), "Invalid demo data");
            return false;
        }
        return true;
    }
    ha->layout_mode = false;

    // In manual mode only the configured entities are updated
    ha_state_parser *sp = ha_parser_begin(ha, cfg, ha->auto_mode);
    if (ha->auto_mode)
    {
        ha->count = 0;
        ha->skipped = 0;
    }
    if (!ha_parser_feed(sp, demo_states_json, strlen(demo_states_json))
        || !ha_parser_end(sp))
    {
        set_str(ha->error, sizeof(ha->error), "Invalid demo data");
        return false;
    }
    if (ha->auto_mode)
        ha_sort(ha);
    return true;
}

// Simulates the result of an action in demo mode
static void demo_action(ha_entity *e, entity_action a)
{
    const char *st = e->state;

    if (a == ACT_PRIMARY)
    {
        switch (e->domain)
        {
            case DOM_COVER:
                st = strcmp(st, "closed") == 0 ? "open" : "closed";
                e->position = strcmp(st, "open") == 0 ? 100.0f : 0.0f;
                break;
            case DOM_LOCK:
                st = strcmp(st, "locked") == 0 ? "unlocked" : "locked";
                break;
            case DOM_CLIMATE:
                st = strcmp(st, "off") == 0 ? "heat" : "off";
                break;
            case DOM_MEDIA_PLAYER:
                st = strcmp(st, "playing") == 0 ? "paused" : "playing";
                break;
            case DOM_VACUUM:
                st = entity_is_on(e) ? "returning" : "cleaning";
                break;
            case DOM_SCENE:
            case DOM_SCRIPT:
            case DOM_BUTTON:
            case DOM_INPUT_BUTTON:
                return;
            default:
                st = strcmp(st, "on") == 0 ? "off" : "on";
                if (e->domain == DOM_LIGHT && strcmp(st, "on") == 0
                    && e->dimmable && e->brightness == ATTR_NONE)
                    e->brightness = 255;
                break;
        }
        set_str(e->state, sizeof(e->state), st);
        return;
    }

    float sign = a == ACT_DECREASE ? -1.0f : 1.0f;
    switch (e->domain)
    {
        case DOM_LIGHT:
        {
            float b = e->brightness == ATTR_NONE ? 0 : e->brightness;
            b += sign * 25.5f;
            if (b > 255)
                b = 255;
            if (b <= 0)
            {
                e->brightness = ATTR_NONE;
                set_str(e->state, sizeof(e->state), "off");
            }
            else
            {
                e->brightness = b;
                set_str(e->state, sizeof(e->state), "on");
            }
            break;
        }
        case DOM_CLIMATE:
            e->target_temperature += sign * (e->target_temp_step != ATTR_NONE
                                                 ? e->target_temp_step : 0.5f);
            break;
        case DOM_MEDIA_PLAYER:
            e->volume += sign * 0.05f;
            if (e->volume < 0)
                e->volume = 0;
            if (e->volume > 1)
                e->volume = 1;
            break;
        case DOM_COVER:
            set_str(e->state, sizeof(e->state), sign > 0 ? "open" : "closed");
            e->position = sign > 0 ? 100.0f : 0.0f;
            break;
        case DOM_NUMBER:
        case DOM_INPUT_NUMBER:
        {
            float v = (float)atof(e->state) + sign * (e->step > 0 ? e->step : 1);
            if (v < e->min_value)
                v = e->min_value;
            if (v > e->max_value)
                v = e->max_value;
            snprintf(e->state, sizeof(e->state), "%.1f", (double)v);
            break;
        }
        default:
            break;
    }
}

bool ha_load_all(ha_client *ha, const ha_config *cfg, ha_progress_cb progress,
                 void *user)
{
    if (ha->demo)
        return demo_load(ha, cfg);

    if (cfg != NULL && cfg->use_layout)
    {
        int r = load_layout(ha, cfg);
        if (r < 0)
            return false;
        if (r > 0)
        {
            // All the states of the layout in one small request
            if (!fetch_states(ha, NULL, "/api/dsha/states", false, progress,
                              user))
                return false;
            ha->error[0] = '\0';
            return true;
        }
    }
    ha->layout_mode = false;

    if (ha->auto_mode)
    {
        ha->count = 0;
        ha->skipped = 0;
        if (!fetch_states(ha, cfg, "/api/states", true, progress, user))
            return false;
        ha_sort(ha);
        return true;
    }

    // Manual mode: one small request per entity is much faster than
    // downloading every state of a big installation.
    for (int i = 0; i < ha->count; i++)
    {
        if (!ha_refresh(ha, &ha->entities[i]))
        {
            // A missing entity isn't fatal, but a network error is
            if (ha->last_status == 0 || ha->last_status == 401
                || ha->last_status == 403)
                return false;
        }
        if (progress)
            progress(user, i + 1, ha->count);
    }
    ha->error[0] = '\0';
    return true;
}

bool ha_refresh(ha_client *ha, ha_entity *e)
{
    if (ha->demo)
    {
        e->pending = false;
        return true;
    }

    char path[128];
    snprintf(path, sizeof(path), "/api/states/%s", e->id);

    bool ok = fetch_states(ha, NULL, path, false, NULL, NULL);
    if (!ok && ha->last_status == 404)
    {
        set_str(e->state, sizeof(e->state), "unavailable");
        set_str(e->extra, sizeof(e->extra), "Entity not found");
        e->loaded = true;
        e->pending = false;
    }
    return ok;
}

bool ha_action(ha_client *ha, ha_entity *e, entity_action a)
{
    char domain[32], service[32], body[192], path[96];

    if (!entity_build_service(e, a, domain, sizeof(domain), service,
                              sizeof(service), body, sizeof(body)))
    {
        set_str(ha->error, sizeof(ha->error), "Action not supported");
        return false;
    }

    if (ha->demo)
    {
        demo_action(e, a);
        e->pending = false;
        return true;
    }

    snprintf(path, sizeof(path), "/api/services/%s/%s", domain, service);

    fetch_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.sp = ha_parser_begin(ha, NULL, false);

    e->pending = true;

    http_result res;
    if (!ha_http(ha, "POST", path, body, fetch_data_cb, &ctx, &res))
    {
        ha->last_status = 0;
        set_str(ha->error, sizeof(ha->error), res.error);
        e->pending = false;
        return false;
    }

    if (!status_ok(ha, &res))
    {
        if (res.status == 400)
            snprintf(ha->error, sizeof(ha->error), "HA rejected %s.%s",
                     domain, service);
        e->pending = false;
        return false;
    }

    // The response contains the states that changed. If ours wasn't in it
    // (the device is slow), keep it as pending so that the UI refreshes it.
    ha_parser_end(ctx.sp);
    return true;
}
