// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Network tests against tests/fake_ha.py. Usage: test_http <host> <port>
// (run_http_tests.py starts the server and runs this program).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char text[512], err[128];
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
    CHECK(requests == 8);
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
