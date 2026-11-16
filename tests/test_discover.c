// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Discovery test: the fake server listens on 127.0.0.<n>:<port>, the scan of
// 127.0.0.0/24 must find it. Usage: test_discover <host> <port>

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "discover.h"
#include "platform.h"
#include "testlib.h"

int main(int argc, char *argv[])
{
    if (argc < 3)
        return 1;
    const char *host = argv[1];
    int port = atoi(argv[2]);

    static discovery d;
    // Pretend the DS is 127.0.0.200
    discovery_start(&d, ntohl(inet_addr("127.0.0.200")), port);
    static const char *const names[] = { "does-not-exist.invalid",
                                         "localhost", NULL };
    discovery_set_names(&d, names);

    uint32_t start = platform_millis();
    int steps = 0;
    while (discovery_step(&d))
    {
        steps++;
        CHECK(d.scanned <= d.total);
        platform_yield();
    }
    uint32_t ms = platform_millis() - start;

    printf("  scan: %d found in %u ms (%d steps)\n", d.num_found, ms, steps);
    for (int i = 0; i < d.num_found; i++)
        printf("  - %s:%d\n", d.found[i].host, d.found[i].port);

    // Found by IP (127.0.0.7) and by name ("localhost" = 127.0.0.1, which
    // isn't listed again by IP)
    CHECK(d.num_found == 2);
    bool by_ip = false, by_name = false;
    for (int i = 0; i < d.num_found; i++)
    {
        by_ip |= strcmp(d.found[i].host, host) == 0;
        by_name |= strcmp(d.found[i].host, "localhost") == 0;
    }
    CHECK(by_ip);
    CHECK(by_name);
    CHECK(d.scanned == d.total);
    CHECK(ms < 20000);

    TEST_REPORT("test_discover");
}
