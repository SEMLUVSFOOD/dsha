// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

void config_defaults(ha_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->port = 8123;
    cfg->refresh_seconds = 15;
    cfg->use_layout = true;
    snprintf(cfg->title, sizeof(cfg->title), "Home Assistant");
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        end--;
    *end = '\0';

    // Remove optional quotes around the value
    size_t len = strlen(s);
    if (len >= 2 && ((s[0] == '"' && s[len - 1] == '"')
                     || (s[0] == '\'' && s[len - 1] == '\'')))
    {
        s[len - 1] = '\0';
        s++;
    }
    return s;
}

static void copy_str(char *dst, size_t size, const char *src)
{
    snprintf(dst, size, "%s", src);
}

static void lowercase(char *s)
{
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

// Accepts "http://host:port/" style URLs as well as plain hosts.
static void parse_host(ha_config *cfg, const char *value)
{
    const char *v = value;
    if (strncmp(v, "http://", 7) == 0)
        v += 7;
    else if (strncmp(v, "https://", 8) == 0)
        v += 8; // TLS isn't supported, but be forgiving with the syntax

    char tmp[128];
    copy_str(tmp, sizeof(tmp), v);

    char *slash = strchr(tmp, '/');
    if (slash)
        *slash = '\0';

    // "host:port", but not IPv6 literals
    char *colon = strrchr(tmp, ':');
    if (colon && strchr(tmp, ':') == colon)
    {
        *colon = '\0';
        int port = atoi(colon + 1);
        if (port > 0 && port < 65536)
            cfg->port = port;
    }

    copy_str(cfg->host, sizeof(cfg->host), tmp);
}

static void add_entities(ha_config *cfg, char *value)
{
    // Allow several comma separated entities in one line
    char *save = NULL;
    for (char *tok = strtok_r(value, ",", &save); tok != NULL;
         tok = strtok_r(NULL, ",", &save))
    {
        char *id = trim(tok);
        if (*id == '\0' || strchr(id, '.') == NULL)
            continue;
        if (cfg->num_entities >= CFG_MAX_ENTITIES)
            return;
        lowercase(id);
        copy_str(cfg->entities[cfg->num_entities], CFG_ENTITY_ID_LEN, id);
        cfg->num_entities++;
    }
}

bool config_parse(ha_config *cfg, const char *text, char *err, size_t errlen)
{
    char line[1024];
    const char *p = text;
    int lineno = 0;

    while (*p != '\0')
    {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = eol ? eol + 1 : p + strlen(p);
        lineno++;

        // Strip UTF-8 BOM on the first line
        char *l = line;
        if (lineno == 1 && (unsigned char)l[0] == 0xEF
            && (unsigned char)l[1] == 0xBB && (unsigned char)l[2] == 0xBF)
            l += 3;

        l = trim(l);
        if (*l == '\0' || *l == '#' || *l == ';' || *l == '[')
            continue;

        char *eq = strchr(l, '=');
        if (eq == NULL)
        {
            // A bare entity id on its own line is accepted too
            if (strchr(l, '.') != NULL && strchr(l, ' ') == NULL)
                add_entities(cfg, l);
            continue;
        }

        *eq = '\0';
        char *key = trim(l);
        char *value = trim(eq + 1);
        lowercase(key);

        if (strcmp(key, "host") == 0 || strcmp(key, "url") == 0)
            parse_host(cfg, value);
        else if (strcmp(key, "port") == 0)
            cfg->port = atoi(value);
        else if (strcmp(key, "token") == 0)
            copy_str(cfg->token, sizeof(cfg->token), value);
        else if (strcmp(key, "refresh_token") == 0)
            copy_str(cfg->refresh_token, sizeof(cfg->refresh_token), value);
        else if (strcmp(key, "wifi_ssid") == 0 || strcmp(key, "ssid") == 0)
            copy_str(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), value);
        else if (strcmp(key, "wifi_password") == 0
                 || strcmp(key, "password") == 0)
            copy_str(cfg->wifi_password, sizeof(cfg->wifi_password), value);
        else if (strcmp(key, "title") == 0)
            copy_str(cfg->title, sizeof(cfg->title), value);
        else if (strcmp(key, "refresh_seconds") == 0
                 || strcmp(key, "refresh") == 0)
            cfg->refresh_seconds = atoi(value);
        else if (strcmp(key, "entity") == 0 || strcmp(key, "entities") == 0)
            add_entities(cfg, value);
        else if (strcmp(key, "use_layout") == 0 || strcmp(key, "layout") == 0)
            cfg->use_layout = !(strcmp(value, "0") == 0
                                || strcmp(value, "false") == 0
                                || strcmp(value, "no") == 0);
        else if (strcmp(key, "demo") == 0)
            cfg->demo = atoi(value) != 0 || strcmp(value, "true") == 0
                        || strcmp(value, "yes") == 0;
        else if (strcmp(key, "domains") == 0)
        {
            lowercase(value);
            copy_str(cfg->domains, sizeof(cfg->domains), value);
        }
        // Unknown keys are ignored so that old versions keep working with
        // newer config files.
    }

    if (cfg->refresh_seconds < 0)
        cfg->refresh_seconds = 0;
    if (cfg->port <= 0 || cfg->port > 65535)
        cfg->port = 8123;

    if (cfg->demo)
        return true;

    if (cfg->host[0] == '\0')
    {
        snprintf(err, errlen, "Missing 'host' in config file");
        return false;
    }
    if (cfg->token[0] == '\0' && cfg->refresh_token[0] == '\0')
    {
        snprintf(err, errlen, "Missing 'token' in config file");
        return false;
    }

    return true;
}

bool config_load_file(ha_config *cfg, const char *path, char *err,
                      size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
    {
        snprintf(err, errlen, "Can't open %s", path);
        return false;
    }

    static char buf[16 * 1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    return config_parse(cfg, buf, err, errlen);
}

size_t config_write(const ha_config *cfg, char *out, size_t out_len)
{
    size_t len = 0;

#define PUT(...)                                                        \
    do {                                                                \
        if (len < out_len)                                              \
        {                                                               \
            int n_ = snprintf(out + len, out_len - len, __VA_ARGS__);   \
            if (n_ > 0)                                                 \
                len += (size_t)n_;                                      \
        }                                                               \
    } while (0)

    PUT("# DSHA configuration, written by the setup wizard.\n"
        "# You can edit it, see config.example.ini for all the options.\n"
        "# Delete this file to run the setup wizard again.\n\n");
    if (cfg->demo)
        PUT("demo = 1\n");
    if (cfg->host[0])
        PUT("host = %s\nport = %d\n", cfg->host, cfg->port);
    if (cfg->token[0])
        PUT("token = %s\n", cfg->token);
    if (cfg->refresh_token[0])
        PUT("# Session created by signing in on the DS. You can revoke it in\n"
            "# your Home Assistant profile (Security > Refresh tokens).\n"
            "refresh_token = %s\n", cfg->refresh_token);
    if (cfg->wifi_ssid[0])
        PUT("\nwifi_ssid = %s\nwifi_password = %s\n", cfg->wifi_ssid,
            cfg->wifi_password);
    PUT("\ntitle = %s\nrefresh_seconds = %d\n", cfg->title,
        cfg->refresh_seconds);
    if (cfg->domains[0])
        PUT("domains = %s\n", cfg->domains);
    if (!cfg->use_layout)
        PUT("use_layout = 0\n");
    if (cfg->num_entities > 0)
        PUT("\n");
    for (int i = 0; i < cfg->num_entities; i++)
        PUT("entity = %s\n", cfg->entities[i]);

#undef PUT

    if (len >= out_len)
        len = out_len - 1;
    return len;
}

bool config_save_file(const ha_config *cfg, const char *path)
{
    static char buf[16 * 1024];
    size_t len = config_write(cfg, buf, sizeof(buf));

    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return false;
    bool ok = fwrite(buf, 1, len, f) == len;
    if (fclose(f) != 0)
        ok = false;
    return ok;
}

bool config_domain_allowed(const ha_config *cfg, const char *entity_id)
{
    if (cfg->domains[0] == '\0')
        return true;

    const char *dot = strchr(entity_id, '.');
    if (dot == NULL)
        return false;
    size_t dlen = (size_t)(dot - entity_id);

    const char *p = cfg->domains;
    while (*p)
    {
        while (*p == ',' || *p == ' ')
            p++;
        const char *start = p;
        while (*p && *p != ',' && *p != ' ')
            p++;
        size_t len = (size_t)(p - start);
        if (len == dlen && strncmp(start, entity_id, len) == 0)
            return true;
    }
    return false;
}
