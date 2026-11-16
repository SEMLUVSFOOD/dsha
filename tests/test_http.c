// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Network tests against tests/fake_ha.py. Usage: test_http <host> <port>
// (run_http_tests.py starts the server and runs this program).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "auth.h"
#include "config.h"
#include "ha.h"
#include "platform.h"
#include "testlib.h"

static ha_config cfg;
static ha_client ha;

static ha_entity *find(const char *id)
{
    for (int i = 0; i < ha.count; i++)
    {
        if (strcmp(ha.entities[i].id, id) == 0)
            return &ha.entities[i];
    }
    return NULL;
}

static void setup(const char *host, int port, const char *token,
                  const char *extra)
{
    char text[1024], err[128];
    snprintf(text, sizeof(text), "host=%s\nport=%d\ntoken=%s\n%s", host, port,
             token, extra);
    config_defaults(&cfg);
    if (!config_parse(&cfg, text, err, sizeof(err)))
    {
        printf("config: %s\n", err);
        exit(1);
    }
    ha_init(&ha, &cfg);
    ha.http.timeout_ms = 3000;
}

static int progress_calls;
static void progress(void *user, int done, int total)
{
    (void)user;
    (void)done;
    (void)total;
    progress_calls++;
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Usage: %s host port\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);

    // Wrong token
    setup(host, port, "wrong", "");
    CHECK(!ha_check(&ha));
    CHECK(ha.last_status == 401);
    CHECK(strstr(ha.error, "token") != NULL);
    http_client_close(&ha.http);

    // Automatic mode
    setup(host, port, "testtoken", "");
    CHECK(ha_check(&ha));
    CHECK(ha_load_all(&ha, &cfg, progress, NULL));
    CHECK(progress_calls > 0);
    CHECK(ha.count == 22);
    if (ha.count != 22)
        printf("  count = %d, error = %s\n", ha.count, ha.error);

    ha_entity *e = find("switch.coffee_machine");
    CHECK(e && strcmp(e->state, "on") == 0);
    CHECK(ha_action(&ha, e, ACT_PRIMARY));
    CHECK(strcmp(e->state, "off") == 0);
    CHECK(!e->pending);
    CHECK(ha_refresh(&ha, e));
    CHECK(strcmp(e->state, "off") == 0);
    CHECK(ha_action(&ha, e, ACT_PRIMARY)); // Back to "on"

    e = find("light.living_room");
    float b = e->brightness;
    CHECK(ha_action(&ha, e, ACT_INCREASE));
    CHECK(e->brightness > b);

    e = find("climate.thermostat");
    CHECK(ha_action(&ha, e, ACT_DECREASE));
    CHECK(e->target_temperature == 21.0f);

    // Scenes return an empty list of changed states
    e = find("scene.movie_night");
    CHECK(ha_action(&ha, e, ACT_PRIMARY));

    // Unavailable entities have no actions
    e = find("fan.bedroom");
    CHECK(!ha_action(&ha, e, ACT_PRIMARY));

    uint32_t requests = ha.http.requests;
    CHECK(requests == 9); // Includes the layout request (404: not installed)
    printf("  %u requests, %u reconnects, %u bytes\n", ha.http.requests,
           ha.http.reconnects, ha.http.bytes_received);
    http_client_close(&ha.http);

    // Manual mode with a missing entity
    setup(host, port, "testtoken",
          "entity=lock.front_door\nentity=light.does_not_exist\n");
    CHECK(ha_load_all(&ha, &cfg, NULL, NULL));
    CHECK(ha.count == 2);
    CHECK(strcmp(ha.entities[0].state, "locked") == 0);
    CHECK(strcmp(ha.entities[1].state, "unavailable") == 0);
    CHECK(strcmp(ha.entities[1].extra, "Entity not found") == 0);
    CHECK(ha_action(&ha, &ha.entities[0], ACT_PRIMARY));
    CHECK(strcmp(ha.entities[0].state, "unlocked") == 0);
    CHECK(ha_action(&ha, &ha.entities[0], ACT_PRIMARY));
    CHECK(strcmp(ha.entities[0].state, "locked") == 0);
    http_client_close(&ha.http);

    // Sign in with user name and password
    // -----------------------------------
    {
        static http_client hc;
        static ha_login login;
        http_client_init(&hc, host, port);
        hc.timeout_ms = 3000;

        CHECK(auth_probe(&hc));

        CHECK(auth_login(&hc, &login, "sam", "wrong") == LOGIN_INVALID);
        CHECK(strstr(login.error, "password") != NULL);

        CHECK(auth_login(&hc, &login, "sam", "secret") == LOGIN_OK);
        CHECK(strncmp(login.refresh_token, "ref-", 4) == 0);
        CHECK(strncmp(login.access_token, "acc-", 4) == 0);

        // Two-factor authentication
        static ha_login login2;
        CHECK(auth_login(&hc, &login2, "mfa", "secret") == LOGIN_NEED_MFA);
        CHECK(auth_login_mfa(&hc, &login2, "000000") == LOGIN_NEED_MFA);
        CHECK(strstr(login2.error, "code") != NULL);
        CHECK(auth_login_mfa(&hc, &login2, "123456") == LOGIN_OK);
        CHECK(login2.refresh_token[0] != '\0');

        // Use the refresh token as the only credential
        char extra[600];
        snprintf(extra, sizeof(extra), "refresh_token=%s\n",
                 login.refresh_token);
        setup(host, port, "", extra);
        CHECK(ha.refresh_token[0] != '\0');
        CHECK(ha_check(&ha));
        CHECK(ha_load_all(&ha, &cfg, NULL, NULL));
        CHECK(ha.count == 22);

        // Access tokens expire: DSHA must get a new one transparently
        http_result r;
        CHECK(http_request(&hc, "POST", "/test/expire_access_tokens", NULL,
                           "{}", NULL, NULL, &r) && r.status == 200);
        ha_entity *sw = find("switch.coffee_machine");
        CHECK(ha_refresh(&ha, sw));
        CHECK(ha_action(&ha, sw, ACT_PRIMARY));
        CHECK(ha_action(&ha, sw, ACT_PRIMARY));

        // The session is deleted in Home Assistant: clear error message
        CHECK(http_request(&hc, "POST", "/test/revoke_refresh_tokens", NULL,
                           "{}", NULL, NULL, &r) && r.status == 200);
        CHECK(!ha_refresh(&ha, sw));
        CHECK(ha.signed_out);
        CHECK(strstr(ha.error, "sign in again") != NULL);
        printf("  revoked: %s\n", ha.error);

        http_client_close(&ha.http);
        http_client_close(&hc);
    }

    // DS Dashboard layout (custom_components/dsha installed)
    // ----------------------------------------------------
    {
        static http_client hc;
        http_client_init(&hc, host, port);
        http_result r;
        char body[8192];
        FILE *f = fopen("fixtures/layout.json", "rb");
        size_t n = f ? fread(body, 1, sizeof(body) - 1, f) : 0;
        if (f)
            fclose(f);
        body[n] = '\0';
        CHECK(n > 0);

        // Not installed: normal (automatic) mode
        setup(host, port, "testtoken", "");
        CHECK(ha_load_all(&ha, &cfg, NULL, NULL));
        CHECK(!ha.layout_mode);
        CHECK(ha.count == 22);
        http_client_close(&ha.http);

        CHECK(http_request(&hc, "POST", "/test/set_layout", NULL, body, NULL,
                           NULL, &r) && r.status == 200);

        // Installed: the pages of the panel, with a manual list in the config
        // (the layout wins)
        setup(host, port, "testtoken", "entity=sensor.humidity\n");
        uint32_t before = ha.http.requests;
        CHECK(ha_load_all(&ha, &cfg, NULL, NULL));
        CHECK(ha.layout_mode);
        CHECK(ha.http.requests - before == 2); // Layout + all states
        CHECK(ha.num_pages == 3);
        CHECK(ha.count == 19);
        CHECK_STR(ha.layout_title, "Demo house");
        ha_entity *e = find("light.living_room");
        CHECK_STR(e->name, "Ceiling");
        CHECK(e->loaded && strcmp(e->state, "on") == 0);
        CHECK(e->brightness > 0);

        // A whole page is refreshed with one request
        before = ha.http.requests;
        CHECK(ha_refresh_page(&ha, 2));
        CHECK(ha.http.requests - before == 1);
        CHECK(!ha_refresh_page(&ha, 9)); // Bad page

        // Trigger an automation (instead of toggling it)
        e = find("automation.lights_at_sunset");
        CHECK(e->tile_action == TILE_ACTION_TRIGGER);
        CHECK(!ha_action(&ha, e, ACT_PRIMARY)); // fake_ha has no "trigger"
        CHECK(strstr(ha.error, "automation.trigger") != NULL);
        http_client_close(&ha.http);

        // use_layout = 0 ignores it
        setup(host, port, "testtoken", "use_layout=0\n");
        CHECK(ha_load_all(&ha, &cfg, NULL, NULL));
        CHECK(!ha.layout_mode);
        CHECK(ha.count == 22);
        http_client_close(&ha.http);

        // An empty layout (nothing arranged yet) falls back too, and the
        // configured entities are kept
        CHECK(http_request(&hc, "POST", "/test/set_layout", NULL,
                           "{\"title\":\"\",\"pages\":[{\"name\":\"Home\","
                           "\"tiles\":[null,null,null,null,null,null,null,null]}]}",
                           NULL, NULL, &r) && r.status == 200);
        setup(host, port, "testtoken", "entity=sensor.humidity\n");
        CHECK(ha_load_all(&ha, &cfg, NULL, NULL));
        CHECK(!ha.layout_mode);
        CHECK(ha.count == 1);
        CHECK(strcmp(ha.entities[0].id, "sensor.humidity") == 0);
        CHECK(ha.entities[0].loaded);
        http_client_close(&ha.http);

        CHECK(http_request(&hc, "POST", "/test/set_layout", NULL, "null",
                           NULL, NULL, &r) && r.status == 200);
        http_client_close(&hc);
    }

    // Nothing listening on this port: must fail quickly with a message
    setup(host, 1, "testtoken", "");
    uint32_t start = platform_millis();
    CHECK(!ha_check(&ha));
    CHECK(platform_millis() - start < 3500);
    CHECK(ha.error[0] != '\0');
    printf("  closed port error: %s\n", ha.error);

    // Unknown host
    setup("does-not-exist.invalid", port, "testtoken", "");
    CHECK(!ha_check(&ha));
    printf("  bad host error: %s\n", ha.error);

    TEST_REPORT("test_http");
}
