// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "auth.h"
#include "discover.h"
#include "platform.h"

// Names that home routers often register for Home Assistant OS (its default
// host name is "homeassistant").
static const char *const default_names[] = {
    "homeassistant",
    "homeassistant.lan",
    "homeassistant.home",
    "homeassistant.fritz.box",
    NULL,
};

void discovery_start(discovery *d, uint32_t ip, int port)
{
    memset(d, 0, sizeof(*d));
    d->port = port;
    d->self_ip = ip;
    d->net = ip & 0xFFFFFF00;
    d->next_host = 1;
    d->names = default_names;
    for (int i = 0; i < DISCOVERY_SLOTS; i++)
        d->slots[i].fd = -1;

    int names = 0;
    while (d->names[names] != NULL)
        names++;
    d->total = names + 254;
}

void discovery_set_names(discovery *d, const char *const *names)
{
    int old = 0, now = 0;
    while (d->names[old] != NULL)
        old++;
    while (names[now] != NULL)
        now++;
    d->names = names;
    d->total += now - old;
}

static void add_found(discovery *d, const char *host, uint32_t ip)
{
    for (int i = 0; i < d->num_found; i++)
    {
        if (d->found[i].ip == ip || strcmp(d->found[i].host, host) == 0)
            return;
    }
    if (d->num_found >= DISCOVERY_MAX_FOUND)
        return;
    snprintf(d->found[d->num_found].host, sizeof(d->found[0].host), "%s", host);
    d->found[d->num_found].port = d->port;
    d->found[d->num_found].ip = ip;
    d->num_found++;
}

// Confirms that host:port is Home Assistant (blocking, but fast on a LAN)
static bool confirm(discovery *d, const char *host)
{
    static http_client c;
    http_client_init(&c, host, d->port);
    c.timeout_ms = 1500;
    bool ok = auth_probe(&c);
    http_client_close(&c);
    return ok;
}

static void close_slot(discovery *d, int i)
{
    if (d->slots[i].fd >= 0)
        close(d->slots[i].fd);
    d->slots[i].fd = -1;
}

static bool open_slot(discovery *d, int i, uint32_t ip)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    int opt = 1;
    ioctl(fd, FIONBIO, (char *)&opt);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)d->port);
    sin.sin_addr.s_addr = htonl(ip);

    int rc = connect(fd, (struct sockaddr *)&sin, sizeof(sin));
    if (rc != 0 && errno != EINPROGRESS && errno != EWOULDBLOCK
        && errno != EAGAIN)
    {
        close(fd);
        return true; // Refused right away: nothing there, slot stays free
    }

    d->slots[i].fd = fd;
    d->slots[i].ip = ip;
    d->slots[i].start_ms = platform_millis();
    return true;
}

static void check_slot(discovery *d, int i)
{
    struct pollfd pfd = { d->slots[i].fd, POLLOUT, 0 };
    int rc = poll(&pfd, 1, 0);

    if (rc > 0)
    {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(d->slots[i].fd, SOL_SOCKET, SO_ERROR, &err, &len);
        uint32_t ip = d->slots[i].ip;
        close_slot(d, i);
        d->scanned++;

        if (err == 0 && (pfd.revents & POLLOUT))
        {
            struct in_addr a;
            a.s_addr = htonl(ip);
            char host[32];
            snprintf(host, sizeof(host), "%s", inet_ntoa(a));
            if (confirm(d, host))
                add_found(d, host, ip);
        }
        return;
    }

    if ((int32_t)(platform_millis() - d->slots[i].start_ms)
        > DISCOVERY_TIMEOUT_MS)
    {
        close_slot(d, i);
        d->scanned++;
    }
}

bool discovery_step(discovery *d)
{
    if (d->done)
        return false;

    // 1. Host names (one per step: DNS lookups block for a moment)
    if (d->names[d->next_name] != NULL)
    {
        const char *name = d->names[d->next_name++];
        d->scanned++;

        struct addrinfo hint, *res = NULL;
        memset(&hint, 0, sizeof(hint));
        hint.ai_family = AF_INET;
        hint.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(name, NULL, &hint, &res) == 0 && res != NULL)
        {
            uint32_t ip = ntohl(((struct sockaddr_in *)res->ai_addr)
                                    ->sin_addr.s_addr);
            freeaddrinfo(res);
            if (confirm(d, name))
                add_found(d, name, ip);
        }
        return true;
    }

    // 2. Subnet scan
    bool busy = false;
    for (int i = 0; i < DISCOVERY_SLOTS; i++)
    {
        if (d->slots[i].fd >= 0)
            check_slot(d, i);

        while (d->slots[i].fd < 0 && d->next_host <= 254)
        {
            uint32_t ip = d->net | (uint32_t)d->next_host++;
            if (ip == d->self_ip)
            {
                d->scanned++;
                continue;
            }
            if (!open_slot(d, i, ip))
            {
                d->next_host--; // Out of sockets, retry later
                break;
            }
            if (d->slots[i].fd < 0)
                d->scanned++; // Refused immediately
        }

        if (d->slots[i].fd >= 0)
            busy = true;
    }

    if (!busy && d->next_host > 254)
    {
        d->done = true;
        d->scanned = d->total;
        return false;
    }
    return true;
}

void discovery_stop(discovery *d)
{
    for (int i = 0; i < DISCOVERY_SLOTS; i++)
        close_slot(d, i);
    d->done = true;
}
