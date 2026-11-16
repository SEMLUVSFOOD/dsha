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

    // Setup wizard screens
    picker_init(&app.picker, "Welcome to DSHA!",
                "Control your Home Assistant from this DS.\n\n"
                "The setup connects to WiFi, finds Home Assistant and signs "
                "you in. It takes about a minute.");
    app.picker.empty_text[0] = '\0';
    picker_set_actions(&app.picker, "Start setup", "Try the demo");
    app_show_picker(&app);
    app_draw(&app, top, bottom);
    save("09_setup_welcome");

    picker_init(&app.picker, "Choose your WiFi network",
                "DSHA couldn't connect with the WiFi settings saved in the "
                "console. Pick your network:");
    picker_add(&app.picker, "Guest", "Open  |  signal 88%");
    picker_add(&app.picker, "MyHome", "WPA2  |  signal 71%");
    picker_add(&app.picker, "Neighbour 5G", "WPA2  |  signal 30%");
    picker_set_actions(&app.picker, "Rescan", "Type name");
    app_show_picker(&app);
    app_draw(&app, top, bottom);
    save("10_setup_wifi");

    kbd_init(&app.kbd, "Password for \"MyHome\"", "hunter2", 64, KBD_TEXT,
             true, true);
    snprintf(app.kbd.hint, sizeof(app.kbd.hint),
             "Type the WiFi password. Press Y to show it.");
    app_show_keyboard(&app);
    app_draw(&app, top, bottom);
    save("11_setup_password");

    picker_init(&app.picker, "Found Home Assistant!",
                "Tap your server, or type its address.");
    app.picker.progress = 100;
    picker_add(&app.picker, "homeassistant", "192.168.1.20:8123");
    picker_set_actions(&app.picker, "Type address", "");
    app_show_picker(&app);
    app_draw(&app, top, bottom);
    save("12_setup_found");

    picker_init(&app.picker, "Sign in to Home Assistant",
                "On your phone or computer (on the same WiFi), open the "
                "address above, type the code and sign in with your Home "
                "Assistant user.\nOr tap \"Type on the DS\".");
    snprintf(app.picker.big_text, sizeof(app.picker.big_text),
             "http://192.168.1.57");
    snprintf(app.picker.big_text2, sizeof(app.picker.big_text2),
             "Code: 482915");
    snprintf(app.picker.empty_text, sizeof(app.picker.empty_text),
             "Waiting for you to sign in on your phone...");
    picker_set_actions(&app.picker, "Type on the DS", "Back");
    app_show_picker(&app);
    app_draw(&app, top, bottom);
    save("13_setup_signin");

    kbd_init(&app.kbd, "Two-factor code", "1234", 8, KBD_NUMERIC, false,
             true);
    snprintf(app.kbd.hint, sizeof(app.kbd.hint),
             "Enter the code from your authenticator app.");
    app_show_keyboard(&app);
    app_draw(&app, top, bottom);
    save("14_setup_code");

    // Layout from the DS Dashboard panel (the demo uses one)
    {
        static ha_config dcfg;
        static ha_client dha;
        config_defaults(&dcfg);
        config_parse(&dcfg, "demo=1\nrefresh_seconds=0\n", err, sizeof(err));
        ha_init(&dha, &dcfg);
        ha_load_all(&dha, &dcfg, NULL, NULL);
        app_init(&app, &dcfg, &dha);
        app.hour = 9;
        app.minute = 41;
        app.wifi_quality = 3;
        app_show_dashboard(&app);
        frames(2, 0);
        save("15_layout_living_room");
        frames(2, IN_SELECT);
        frames(2, IN_SELECT);
        frames(2, IN_DOWN);
        frames(2, IN_DOWN);
        save("16_layout_security");
    }

    printf("render: ok, %u requests\n", ha.http.requests);
    return 0;
}
