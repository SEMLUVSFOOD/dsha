// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Runs the real application logic against tests/fake_ha.py with scripted
// input and saves screenshots of both screens (as PPM files). This is how the
// screenshots of the documentation are made, and it's a handy way to check UI
// changes without a DS. Usage: render <host> <port> <output dir>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "platform.h"

static ha_config cfg;
static ha_client ha;
static app_t app;
static color_t top[GFX_W * GFX_H];
static color_t bottom[GFX_W * GFX_H];
static const char *outdir;

#define GAP 8

static void save(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H * 2 + GAP);
    for (int y = 0; y < GFX_H * 2 + GAP; y++)
    {
        for (int x = 0; x < GFX_W; x++)
        {
            unsigned char rgb[3] = { 0, 0, 0 };
            const color_t *fb = NULL;
            int yy = y;
            if (y < GFX_H)
                fb = top;
            else if (y >= GFX_H + GAP)
            {
                fb = bottom;
                yy = y - GFX_H - GAP;
            }
            if (fb)
            {
                color_t c = fb[yy * GFX_W + x];
                rgb[0] = (unsigned char)(((c >> 0) & 31) * 255 / 31);
                rgb[1] = (unsigned char)(((c >> 5) & 31) * 255 / 31);
                rgb[2] = (unsigned char)(((c >> 10) & 31) * 255 / 31);
            }
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

// Runs some frames with the given input pressed on the first one
static void frames(int n, uint32_t keys)
{
    for (int i = 0; i < n; i++)
    {
        app_input in = { 0 };
        if (i == 0)
            in.down = keys;
        if (app_update(&app, &in, platform_millis()))
            app_draw(&app, top, bottom);
    }
}

static void touch(int x, int y)
{
    app_input in = { 0 };
    in.touching = true;
    in.tx = x;
    in.ty = y;
    app_update(&app, &in, platform_millis());
    in.touching = false;
    app_update(&app, &in, platform_millis());
    frames(4, 0);
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        printf("Usage: %s host port outdir\n", argv[0]);
        return 1;
    }
    outdir = argv[3];

    char text[256], err[128];
    snprintf(text, sizeof(text),
             "host=%s\nport=%s\ntoken=testtoken\ntitle=My Home\n"
             "refresh_seconds=0\n", argv[1], argv[2]);
    config_defaults(&cfg);
    if (!config_parse(&cfg, text, err, sizeof(err)))
        return 1;
    ha_init(&ha, &cfg);
    app_init(&app, &cfg, &ha);

    // The HTTP client has its own copy of the address. Show a realistic one
    // in the screenshots instead of the local test server.
    snprintf(cfg.host, sizeof(cfg.host), "192.168.1.20");
    cfg.port = 8123;
    app.hour = 9;
    app.minute = 41;
    app.wifi_quality = 3;
    snprintf(app.ip, sizeof(app.ip), "192.168.1.57");

    app_show_message(&app, "Loading entities...",
                     "Connected to 192.168.1.20:8123\n\nReceived 14 states",
                     "Hold START+SELECT to quit", false, true);
    app.frame = 30;
    app_draw(&app, top, bottom);
    save("01_loading");

    if (!ha_check(&ha) || !ha_load_all(&ha, &cfg, NULL, NULL))
    {
        printf("Load failed: %s\n", ha.error);
        return 1;
    }
    app_show_dashboard(&app);
    frames(3, 0);
    save("02_dashboard");

    // Brighten the living room light with R
    frames(2, IN_DOWN);
    frames(3, IN_R);
    save("03_brightness");

    app.toast[0] = '\0';

    // Climate
    while (app.filter != FILTER_CLIMATE)
        frames(1, IN_SELECT);
    frames(3, 0);
    frames(3, IN_L);
    save("04_climate");

    // Sensors page
    while (app.filter != FILTER_SENSORS)
        frames(1, IN_SELECT);
    frames(3, 0);
    frames(2, IN_DOWN);
    frames(2, IN_RIGHT);
    save("05_sensors");

    // All entities, page 2, tap a tile (toggles it)
    while (app.filter != FILTER_ALL)
        frames(1, IN_SELECT);
    frames(1, 0);
    touch(230, 178); // Next page
    touch(60, 60);
    frames(5, 0);
    app.toast[0] = '\0';
    app_draw(&app, top, bottom);
    save("06_tap_toggle");

    frames(2, IN_X);
    save("07_info");
    frames(2, IN_B);

    app_show_message(&app, "Can't connect to WiFi",
                     "DSHA uses the WiFi settings of the console (or "
                     "wifi_ssid/wifi_password in config.ini).\n\n"
                     "On a DS/DS Lite only open and WEP networks work. "
                     "WPA2 needs a DSi.",
                     "A: retry", true, false);
    app_draw(&app, top, bottom);
    save("08_error");

    printf("render: ok, %u requests\n", ha.http.requests);
    return 0;
}
