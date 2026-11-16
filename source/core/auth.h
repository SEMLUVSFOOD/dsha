// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Sign in to Home Assistant with a user name and password, the same way the
// Home Assistant apps do (the "login flow"), so nobody has to copy a long
// access token by hand. The result is a refresh token that is stored in the
// config file and exchanged for short-lived access tokens when needed.
//
// https://contributors.home-assistant.io/core/internals/auth-api/

#ifndef DSHA_AUTH_H
#define DSHA_AUTH_H

#include <stdbool.h>

#include "http.h"

// Identifies DSHA in the list of refresh tokens of the user's profile. The
// redirect URI must have the same scheme and host as the client id.
#define DSHA_CLIENT_ID    "https://github.com/semluvsfood/dsha"
#define DSHA_REDIRECT_URI "https://github.com/semluvsfood/dsha/auth"

#define AUTH_TOKEN_LEN 512

typedef enum {
    LOGIN_OK,          // Signed in: refresh_token and access_token are valid
    LOGIN_NEED_MFA,    // Ask the user for a two-factor code, then call
                       // auth_login_mfa()
    LOGIN_INVALID,     // Wrong user name, password or code. Try again.
    LOGIN_ERROR,       // Network or server error (see error)
} login_result;

typedef struct {
    char flow_id[64];
    char handler_type[32]; // Auth provider ("homeassistant", ...)
    char handler_id[32];

    char refresh_token[AUTH_TOKEN_LEN];
    char access_token[AUTH_TOKEN_LEN];

    char error[128];
} ha_login;

// Checks that host:port is a Home Assistant server (GET /auth/providers,
// which doesn't need authentication). Returns false if it isn't.
bool auth_probe(http_client *c);

// Starts the login flow and sends the user name and password.
login_result auth_login(http_client *c, ha_login *l, const char *username,
                        const char *password);

// Sends the two-factor authentication code after LOGIN_NEED_MFA.
login_result auth_login_mfa(http_client *c, ha_login *l, const char *code);

// Gets a new access token from a refresh token. Returns false on error. If
// the refresh token isn't valid anymore, *revoked is set to true.
bool auth_refresh(http_client *c, const char *refresh_token,
                  char *access_token, size_t access_len, bool *revoked,
                  char *error, size_t error_len);

#endif // DSHA_AUTH_H
