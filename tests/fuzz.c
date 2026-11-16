// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Mutation fuzzer for everything that parses data coming from the network or
// the SD card. Built with AddressSanitizer and UBSan, so any out of bounds
// access, overflow or undefined behaviour aborts the run.
//
// Usage: fuzz [iterations] [seed]

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "ha.h"
#include "json_get.h"
#include "json_stream.h"

bool http_parse_response_for_test(const char *data, size_t len, size_t chunk,
                                  http_data_cb cb, void *user, int *status,
                                  bool *complete);

static uint64_t rng;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)rng;
}

static const char *const dictionary[] = {
    "{", "}", "[", "]", "\"", ":", ",", "null", "true", "false", "-1e999",
    "\\u", "\\ud83d", "\\\\", "\\\"", "entity_id", "attributes", "state",
    "pages", "tiles", "name", "action", "title", "\r\n", "\r\n\r\n",
    "Content-Length: ", "Transfer-Encoding: chunked\r\n", "HTTP/1.1 200 OK",
    "ffffffffffffffffffff", "0\r\n\r\n", "7fffffff", "-5", "99999999999",
    "supported_color_modes", "providers", "errors", "base", "=", "\n",
    "host", "token", "entity", "port", "\xC3", "\xF0\x9F\x98", "\xFF",
};

// Mutates "in" into "out" (both NUL terminated)
static size_t mutate(const char *in, size_t in_len, char *out, size_t out_max)
{
    size_t len = in_len < out_max ? in_len : out_max;
    memcpy(out, in, len);

    int n = 1 + (int)(rnd() % 8);
    for (int i = 0; i < n; i++)
    {
        size_t pos = len ? rnd() % (len + 1) : 0;
        switch (rnd() % 6)
        {
            case 0: // Flip a byte
                if (len)
                    out[rnd() % len] = (char)rnd();
                break;
            case 1: // Delete a range
                if (len)
                {
                    size_t del = 1 + rnd() % 16;
                    if (pos + del > len)
                        del = len - pos;
                    memmove(out + pos, out + pos + del, len - pos - del);
                    len -= del;
                }
                break;
            case 2: // Insert a dictionary word
            {
                const char *w = dictionary[rnd() % (sizeof(dictionary)
                                                    / sizeof(dictionary[0]))];
                size_t wl = strlen(w);
                if (len + wl < out_max)
                {
                    memmove(out + pos + wl, out + pos, len - pos);
                    memcpy(out + pos, w, wl);
                    len += wl;
                }
                break;
            }
            case 3: // Duplicate a range (deep nesting, long strings)
            {
                size_t dl = 1 + rnd() % 64;
                if (pos + dl <= len && len + dl < out_max)
                {
                    memmove(out + pos + dl, out + pos, len - pos);
                    memcpy(out + pos + dl, out + pos, 0);
                    len += dl;
                }
                break;
            }
            case 4: // Truncate
                len = pos;
                break;
            default: // Random byte
                if (len < out_max)
                {
                    memmove(out + pos + 1, out + pos, len - pos);
                    out[pos] = (char)rnd();
                    len++;
                }
                break;
        }
    }
    out[len < out_max ? len : out_max - 1] = '\0';
    return len;
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        printf("Can't open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
        exit(1);
    buf[n] = '\0';
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static bool sink(void *user, const char *data, size_t len)
{
    // Touch every byte so ASan sees out of bounds reads
    volatile unsigned sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += (unsigned char)data[i];
    (void)user;
    return true;
}

static void nop_cb(void *u, json_event ev, const char *v, int d)
{
    (void)u;
    (void)ev;
    if (v)
    {
        volatile size_t l = strlen(v);
        (void)l;
    }
    (void)d;
}

int main(int argc, char *argv[])
{
    long iterations = argc > 1 ? atol(argv[1]) : 200000;
    rng = argc > 2 ? strtoull(argv[2], NULL, 10) : 0x1234567;

    size_t states_len, layout_len;
    char *states = read_file("fixtures/states.json", &states_len);
    char *layout = read_file("fixtures/layout.json", &layout_len);
    static const char http_seed[] =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "5;x=y\r\nhello\r\n3\r\nabc\r\n0\r\nTrailer: 1\r\n\r\n";
    static const char http_seed2[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\n"
        "hello";
    static const char config_seed[] =
        "host = http://192.168.1.2:8123/\ntoken = abc\nentity = light.a, "
        "switch.b\nrefresh_token=xyz\nwifi_ssid=Net\ndomains=light\n";
    static const char auth_seed[] =
        "{\"type\":\"form\",\"flow_id\":\"abc\",\"errors\":{\"base\":"
        "\"invalid_auth\"},\"providers\":[{\"type\":\"homeassistant\","
        "\"id\":null}]}";

    static char buf[64 * 1024];
    static ha_client ha;
    static ha_config cfg;
    char err[128];

    for (long it = 0; it < iterations; it++)
    {
        size_t len;
        switch (it % 6)
        {
            case 0: // States parser (streaming, random chunk sizes)
            {
                len = mutate(states, states_len, buf, sizeof(buf) - 1);
                config_defaults(&cfg);
                ha_init(&ha, &cfg);
                ha_state_parser *sp = ha_parser_begin(&ha, &cfg, true);
                size_t chunk = 1 + rnd() % 300;
                for (size_t i = 0; i < len; i += chunk)
                    ha_parser_feed(sp, buf + i, len - i < chunk ? len - i
                                                                : chunk);
                ha_parser_end(sp);
                break;
            }
            case 1: // Layout parser
                len = mutate(layout, layout_len, buf, sizeof(buf) - 1);
                config_defaults(&cfg);
                ha_init(&ha, &cfg);
                ha_parse_layout(&ha, buf);
                for (int p = 0; p < ha.num_pages; p++)
                    for (int s = 0; s < LAYOUT_SLOTS; s++)
                        if (ha.pages[p].slots[s] >= ha.count)
                        {
                            printf("bad slot index\n");
                            return 1;
                        }
                break;
            case 2: // HTTP response parser
                len = (rnd() & 1)
                          ? mutate(http_seed, sizeof(http_seed) - 1, buf, 4096)
                          : mutate(http_seed2, sizeof(http_seed2) - 1, buf,
                                   4096);
                {
                    int status;
                    bool complete;
                    http_parse_response_for_test(buf, len, 1 + rnd() % 40,
                                                 sink, NULL, &status,
                                                 &complete);
                }
                break;
            case 3: // Config file
                mutate(config_seed, sizeof(config_seed) - 1, buf, 4096);
                config_defaults(&cfg);
                config_parse(&cfg, buf, err, sizeof(err));
                {
                    static char out[16 * 1024];
                    config_write(&cfg, out, sizeof(out));
                }
                break;
            case 4: // json_get (auth responses)
            {
                mutate(auth_seed, sizeof(auth_seed) - 1, buf, 4096);
                char a[32], b[32], c[32];
                json_field f[] = {
                    { "type", a, sizeof(a), false },
                    { "errors.base", b, sizeof(b), false },
                    { "providers.0.type", c, sizeof(c), false },
                };
                json_get(buf, f, 3);
                break;
            }
            default: // Raw JSON stream with random bytes
            {
                len = rnd() % 2048;
                for (size_t i = 0; i < len; i++)
                {
                    const char *alphabet = "{}[]\",:0123456789-.eEtrufalsn \\u";
                    buf[i] = (rnd() % 4) ? alphabet[rnd() % 33] : (char)rnd();
                }
                json_parser p;
                json_init(&p, nop_cb, NULL);
                json_feed(&p, buf, len);
                json_finish(&p);
                break;
            }
        }
    }

    printf("fuzz: %ld iterations, no crashes\n", iterations);
    free(states);
    free(layout);
    return 0;
}
