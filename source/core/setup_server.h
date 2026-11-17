// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// "Set up from your phone": during setup the DS serves a small web page with
// a sign in form. Typing a password on a phone is a lot easier than on a DS,
// and a long-lived token can be pasted too. The page shows the progress
// reported by the DS (signing in, two-factor code, done).
//
// Everything is non-blocking: call setup_server_poll() every frame.

#ifndef DSHA_SETUP_SERVER_H
#define DSHA_SETUP_SERVER_H

#include <stdbool.h>

#include "auth.h"
#include "config.h"

typedef enum {
    SETUP_NOTHING,   // No new request
    SETUP_SUBMITTED, // The form was submitted: call setup_process()
} setup_event;

typedef enum {
    SETUP_STATE_WAITING,  // Waiting for the form
    SETUP_STATE_MFA,      // Waiting for the two-factor code
    SETUP_STATE_DONE,     // Signed in
} setup_state;

// Wrong pairing codes allowed before a new code is generated
#define SETUP_MAX_CODE_ATTEMPTS 5

typedef struct {
    int listen_fd;
    int port;

    // Pairing code shown on the DS. The page only accepts a submission with
    // it, so nobody else on the network (or a web page open on the phone)
    // can configure the DS.
    char pin[8];
    int pin_failures;
    bool code_changed; // Set when the DS should redraw (new code, errors)

    setup_state state;
    char status[160];  // Shown on the phone page (and on the DS)
    bool status_error;

    // Prefilled in the form
    char default_server[140];

    // Submitted values
    bool is_mfa;       // The submission is a two-factor code
    char server[140];
    char username[96];
    char password[128];
    char token[512];
    char code[16];
    char pin_entered[16];

    // Login in progress
    ha_login login;
    char login_host[128];
    int login_port;
    bool login_https;
} setup_server;

// Starts listening. Returns false if the socket can't be opened.
bool setup_server_start(setup_server *s, int port, const char *default_server);

// Handles at most one HTTP request. Returns SETUP_SUBMITTED when a form has
// been submitted.
setup_event setup_server_poll(setup_server *s);

void setup_server_stop(setup_server *s);

// Generates a new pairing code
void setup_server_new_pin(setup_server *s);

// Signs in with the submitted values (blocking, a fraction of a second on a
// LAN). On success it fills host, port and token/refresh_token in cfg and
// returns true. Progress and errors are written to s->status.
bool setup_process(setup_server *s, ha_config *cfg);

// The same sign in logic, used by the on-screen keyboard version of the
// setup. Returns the login result; on LOGIN_OK cfg is updated.
login_result setup_sign_in(setup_server *s, ha_config *cfg,
                           const char *server, const char *username,
                           const char *password, const char *token);
login_result setup_sign_in_mfa(setup_server *s, ha_config *cfg,
                               const char *code);

// Splits "http://host:port/" or "https://host/" into host, port (default
// 8123, or 443 for https://) and https.
void setup_parse_server(const char *server, char *host, size_t host_len,
                        int *port, bool *https);

#endif // DSHA_SETUP_SERVER_H
