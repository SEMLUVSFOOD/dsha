// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Runs the phone setup server like the DS does. run_http_tests.py plays the
// role of the phone. Usage: test_setup <default server> <listen port>
// Prints the resulting credentials and exits when the setup has finished.

#include <stdio.h>
#include <stdlib.h>

#include "platform.h"
#include "setup_server.h"

int main(int argc, char *argv[])
{
    if (argc < 3)
        return 1;

    static setup_server s;
    static ha_config cfg;
    config_defaults(&cfg);

    if (!setup_server_start(&s, atoi(argv[2]), argv[1]))
    {
        printf("Can't listen\n");
        return 1;
    }
    printf("listening\n");
    fflush(stdout);

    uint32_t start = platform_millis();
    uint32_t done_at = 0;
    while (platform_millis() - start < 30000)
    {
        if (setup_server_poll(&s) == SETUP_SUBMITTED)
        {
            setup_process(&s, &cfg);
            printf("status: %s\n", s.status);
            fflush(stdout);
        }
        if (s.state == SETUP_STATE_DONE && done_at == 0)
            done_at = platform_millis();
        // Keep answering a moment so the phone can see the final page
        if (done_at && platform_millis() - done_at > 1500)
            break;
        platform_yield();
    }
    setup_server_stop(&s);

    printf("result: host=%s port=%d token=%s refresh=%s\n", cfg.host, cfg.port,
           cfg.token, cfg.refresh_token);
    return s.state == SETUP_STATE_DONE ? 0 : 1;
}
