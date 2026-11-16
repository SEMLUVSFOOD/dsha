// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <stdio.h>
#include <string.h>

#include "auth.h"
#include "json_get.h"

// Collects a whole (small) response body in memory
typedef struct {
    char *buf;
    size_t len;
    size_t size;
} body_buf;

static bool collect_cb(void *user, const char *data, size_t len)
{
    body_buf *b = user;
    if (b->len + len >= b->size)
        len = b->size - 1 - b->len; // Truncate, the fields we need come first
    memcpy(b->buf + b->len, data, len);
    b->len += len;
    b->buf[b->len] = '\0';
    return true;
}

static char response[4096];

// Does a request and stores the body in "response" whatever the status code
static bool request(http_client *c, const char *method, const char *path,
                    const char *body, const char *content_type,
                    http_result *res)
{
    body_buf b = { response, 0, sizeof(response) };
    response[0] = '\0';
    return http_request_ex(c, method, path, NULL, body, content_type, true,
                           collect_cb, &b, res);
}

// Escapes a string to be put inside a JSON string literal
static void json_escape(char *out, size_t out_len, const char *in)
{
    size_t len = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++)
    {
        if (len + 7 >= out_len)
            break;
        if (*p == '"' || *p == '\\')
        {
            out[len++] = '\\';
            out[len++] = (char)*p;
        }
        else if (*p < 0x20)
        {
            len += (size_t)snprintf(out + len, out_len - len, "\\u%04x", *p);
        }
        else
        {
            out[len++] = (char)*p;
        }
    }
    out[len] = '\0';
}

bool auth_probe(http_client *c)
{
    http_result res;
    if (!request(c, "GET", "/auth/providers", NULL, NULL, &res))
        return false;
    if (res.status != 200)
        return false;

    // Newer versions: {"providers": [...]}, older ones: [...]
    char type_new[32], type_old[32];
    json_field f[] = {
        { "providers.0.type", type_new, sizeof(type_new), false },
        { "0.type", type_old, sizeof(type_old), false },
    };
    if (!json_get(response, f, 2))
        return false;
    return f[0].found || f[1].found;
}

static void set_error(ha_login *l, const char *msg, const http_result *res)
{
    if (res != NULL && res->status == 0)
        snprintf(l->error, sizeof(l->error), "%s", res->error);
    else if (res != NULL)
        snprintf(l->error, sizeof(l->error), "%s (HTTP %d)", msg, res->status);
    else
        snprintf(l->error, sizeof(l->error), "%s", msg);
}

// Exchanges the authorization code from the login flow for tokens
static login_result get_tokens(http_client *c, ha_login *l, const char *code)
{
    char body[512] = "grant_type=authorization_code&client_id=";
    http_urlencode(body, sizeof(body), DSHA_CLIENT_ID);
    strncat(body, "&code=", sizeof(body) - strlen(body) - 1);
    http_urlencode(body, sizeof(body), code);

    http_result res;
    if (!request(c, "POST", "/auth/token", body,
                 "application/x-www-form-urlencoded", &res))
    {
        set_error(l, "", &res);
        return LOGIN_ERROR;
    }
    if (res.status != 200)
    {
        set_error(l, "Can't get the access token", &res);
        return LOGIN_ERROR;
    }

    json_field f[] = {
        { "access_token", l->access_token, sizeof(l->access_token), false },
        { "refresh_token", l->refresh_token, sizeof(l->refresh_token), false },
    };
    if (!json_get(response, f, 2) || !f[0].found || !f[1].found)
    {
        set_error(l, "Invalid token response", NULL);
        return LOGIN_ERROR;
    }
    return LOGIN_OK;
}

// Handles the response of a step of the login flow
static login_result flow_response(http_client *c, ha_login *l,
                                  const http_result *res)
{
    char type[32], step[32], result[128], base_err[48], msg[96];
    json_field f[] = {
        { "type", type, sizeof(type), false },
        { "step_id", step, sizeof(step), false },
        { "result", result, sizeof(result), false },
        { "errors.base", base_err, sizeof(base_err), false },
        { "flow_id", l->flow_id, sizeof(l->flow_id), false },
        { "message", msg, sizeof(msg), false },
    };
    json_get(response, f, 6);

    if (res->status != 200)
    {
        if (res->status == 400 && msg[0])
            snprintf(l->error, sizeof(l->error), "%s", msg);
        else
            set_error(l, "Sign in failed", res);
        return LOGIN_ERROR;
    }

    if (strcmp(type, "create_entry") == 0 && result[0] != '\0')
        return get_tokens(c, l, result);

    if (strcmp(type, "form") == 0)
    {
        if (strcmp(base_err, "invalid_auth") == 0)
        {
            snprintf(l->error, sizeof(l->error),
                     "Wrong user name or password");
            return LOGIN_INVALID;
        }
        if (strcmp(base_err, "invalid_code") == 0)
        {
            snprintf(l->error, sizeof(l->error), "Wrong code, try again");
            return strcmp(step, "mfa") == 0 ? LOGIN_NEED_MFA : LOGIN_INVALID;
        }
        if (base_err[0] != '\0')
        {
            snprintf(l->error, sizeof(l->error), "Sign in error: %s",
                     base_err);
            return LOGIN_ERROR;
        }
        if (strcmp(step, "mfa") == 0)
        {
            l->error[0] = '\0';
            return LOGIN_NEED_MFA;
        }
    }

    if (strcmp(type, "abort") == 0)
        snprintf(l->error, sizeof(l->error), "Sign in aborted by the server");
    else
        snprintf(l->error, sizeof(l->error), "Unexpected answer: %s", type);
    return LOGIN_ERROR;
}

// Picks the auth provider: the user/password one if there is one
static void pick_provider(http_client *c, ha_login *l)
{
    snprintf(l->handler_type, sizeof(l->handler_type), "homeassistant");
    l->handler_id[0] = '\0';

    http_result res;
    if (!request(c, "GET", "/auth/providers", NULL, NULL, &res)
        || res.status != 200)
        return;

    char t[4][32], id[4][32];
    json_field f[8];
    for (int i = 0; i < 4; i++)
    {
        static char paths[8][24];
        snprintf(paths[i * 2], sizeof(paths[0]), "providers.%d.type", i);
        snprintf(paths[i * 2 + 1], sizeof(paths[0]), "providers.%d.id", i);
        f[i * 2] = (json_field){ paths[i * 2], t[i], sizeof(t[i]), false };
        f[i * 2 + 1] = (json_field){ paths[i * 2 + 1], id[i], sizeof(id[i]),
                                     false };
    }
    json_get(response, f, 8);

    for (int i = 0; i < 4; i++)
    {
        if (strcmp(t[i], "homeassistant") == 0)
        {
            snprintf(l->handler_type, sizeof(l->handler_type), "%s", t[i]);
            snprintf(l->handler_id, sizeof(l->handler_id), "%s", id[i]);
            return;
        }
    }
    if (t[0][0] != '\0')
    {
        snprintf(l->handler_type, sizeof(l->handler_type), "%s", t[0]);
        snprintf(l->handler_id, sizeof(l->handler_id), "%s", id[0]);
    }
}

login_result auth_login(http_client *c, ha_login *l, const char *username,
                        const char *password)
{
    char body[600];
    http_result res;

    l->error[0] = '\0';
    pick_provider(c, l);

    // 1. Start the flow
    char handler_id[48];
    if (l->handler_id[0] != '\0')
        snprintf(handler_id, sizeof(handler_id), "\"%s\"", l->handler_id);
    else
        snprintf(handler_id, sizeof(handler_id), "null");

    snprintf(body, sizeof(body),
             "{\"client_id\":\"%s\",\"handler\":[\"%s\",%s],"
             "\"redirect_uri\":\"%s\",\"type\":\"authorize\"}",
             DSHA_CLIENT_ID, l->handler_type, handler_id, DSHA_REDIRECT_URI);

    if (!request(c, "POST", "/auth/login_flow", body, "application/json",
                 &res))
    {
        set_error(l, "", &res);
        return LOGIN_ERROR;
    }

    l->flow_id[0] = '\0';
    login_result r = flow_response(c, l, &res);
    if (l->flow_id[0] == '\0')
        return r == LOGIN_OK ? LOGIN_ERROR : r;

    // 2. Send the credentials
    char user_esc[160], pass_esc[200];
    json_escape(user_esc, sizeof(user_esc), username);
    json_escape(pass_esc, sizeof(pass_esc), password);
    snprintf(body, sizeof(body),
             "{\"client_id\":\"%s\",\"username\":\"%s\",\"password\":\"%s\"}",
             DSHA_CLIENT_ID, user_esc, pass_esc);

    char path[128];
    snprintf(path, sizeof(path), "/auth/login_flow/%s", l->flow_id);
    if (!request(c, "POST", path, body, "application/json", &res))
    {
        set_error(l, "", &res);
        return LOGIN_ERROR;
    }
    return flow_response(c, l, &res);
}

login_result auth_login_mfa(http_client *c, ha_login *l, const char *code)
{
    char body[256], code_esc[64], path[128];
    http_result res;

    json_escape(code_esc, sizeof(code_esc), code);
    snprintf(body, sizeof(body), "{\"client_id\":\"%s\",\"code\":\"%s\"}",
             DSHA_CLIENT_ID, code_esc);
    snprintf(path, sizeof(path), "/auth/login_flow/%s", l->flow_id);

    if (!request(c, "POST", path, body, "application/json", &res))
    {
        set_error(l, "", &res);
        return LOGIN_ERROR;
    }
    return flow_response(c, l, &res);
}

bool auth_refresh(http_client *c, const char *refresh_token,
                  char *access_token, size_t access_len, bool *revoked,
                  char *error, size_t error_len)
{
    char body[512] = "grant_type=refresh_token&client_id=";
    http_urlencode(body, sizeof(body), DSHA_CLIENT_ID);
    strncat(body, "&refresh_token=", sizeof(body) - strlen(body) - 1);
    http_urlencode(body, sizeof(body), refresh_token);

    *revoked = false;

    http_result res;
    if (!request(c, "POST", "/auth/token", body,
                 "application/x-www-form-urlencoded", &res))
    {
        snprintf(error, error_len, "%s", res.error);
        return false;
    }

    if (res.status == 400 || res.status == 403)
    {
        // invalid_grant: the token was deleted or expired
        *revoked = true;
        snprintf(error, error_len,
                 "Signed out by Home Assistant. Please sign in again.");
        return false;
    }
    if (res.status != 200)
    {
        snprintf(error, error_len, "Can't refresh the session (HTTP %d)",
                 res.status);
        return false;
    }

    json_field f[] = { { "access_token", access_token, access_len, false } };
    if (!json_get(response, f, 1) || !f[0].found)
    {
        snprintf(error, error_len, "Invalid token response");
        return false;
    }
    return true;
}
