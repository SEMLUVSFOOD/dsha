// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Nintendo DS entry point: video setup, input, WiFi connection, loading the
// config file and the main loop. Everything else lives in source/core.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <nds.h>
#include <dswifi9.h>
#include <fat.h>
#include <filesystem.h>

#include "app.h"
#include "config.h"
#include "ha.h"
#include "platform.h"

// Platform functions
// ------------------

static volatile uint32_t vblank_count;

static void vblank_handler(void)
{
    vblank_count++;
}

void platform_yield(void)
{
    cothread_yield();
}

uint32_t platform_millis(void)
{
    // The DS refreshes at 59.8261 Hz. 1000 / 59.8261 = 16.7151 ms per frame
    return (uint32_t)(((uint64_t)vblank_count * 167151) / 10000);
}

// Globals
// -------

static ha_config cfg;
static ha_client ha;
static app_t app;

// Back buffers in main RAM, copied to VRAM after drawing
static color_t top_buf[GFX_W * GFX_H] ALIGN(32);
static color_t bottom_buf[GFX_W * GFX_H] ALIGN(32);
static u16 *vram_top;
static u16 *vram_bottom;

static bool fat_ok;
static bool nitro_ok;

// Keys held in the last call to read_input(). scanKeys() must only be called
// once per frame, or keysDown() would miss key presses.
static uint32_t held_keys;

static void present(void)
{
    app_draw(&app, top_buf, bottom_buf);
    DC_FlushRange(top_buf, sizeof(top_buf));
    DC_FlushRange(bottom_buf, sizeof(bottom_buf));
    dmaCopy(top_buf, vram_top, sizeof(top_buf));
    dmaCopy(bottom_buf, vram_bottom, sizeof(bottom_buf));
}

static void update_clock(void)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (tm && (tm->tm_hour != app.hour || tm->tm_min != app.minute))
    {
        app.hour = tm->tm_hour;
        app.minute = tm->tm_min;
        app.dirty = true;
    }
}

static void read_input(app_input *in)
{
    scanKeys();
    uint32_t down = keysDown();
    uint32_t held = keysHeld();
    held_keys = held;

    static const struct { uint32_t hw, app; } map[] = {
        { KEY_A, IN_A }, { KEY_B, IN_B }, { KEY_X, IN_X }, { KEY_Y, IN_Y },
        { KEY_L, IN_L }, { KEY_R, IN_R }, { KEY_START, IN_START },
        { KEY_SELECT, IN_SELECT }, { KEY_UP, IN_UP }, { KEY_DOWN, IN_DOWN },
        { KEY_LEFT, IN_LEFT }, { KEY_RIGHT, IN_RIGHT },
    };

    memset(in, 0, sizeof(*in));
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
    {
        if (down & map[i].hw)
            in->down |= map[i].app;
        if (held & map[i].hw)
            in->held |= map[i].app;
    }

    if (held & KEY_TOUCH)
    {
        touchPosition touch;
        touchRead(&touch);
        in->touching = true;
        in->tx = touch.px;
        in->ty = touch.py;
    }
}

// START + SELECT held: exit to the loader
static bool wants_exit(void)
{
    return (held_keys & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT);
}

// Waits for a frame, redrawing the screen if needed. Returns the keys pressed
static uint32_t wait_frame(void)
{
    cothread_yield_irq(IRQ_VBLANK);
    app_input in;
    read_input(&in);
    update_clock();
    if (app_update(&app, &in, platform_millis()))
        present();
    return in.down;
}

// Shows a message and waits until one of the keys in "mask" is pressed
static uint32_t message_wait(const char *title, const char *body,
                             const char *hint, bool error, uint32_t mask)
{
    app_show_message(&app, title, body, hint, error, false);
    while (1)
    {
        uint32_t k = wait_frame();
        if (wants_exit())
            exit(0);
        if (k & mask)
            return k;
    }
}

// Config file
// -----------

static const char *config_paths[] = {
    "sd:/dsha/config.ini",
    "fat:/dsha/config.ini",
    "sd:/dsha.ini",
    "fat:/dsha.ini",
    "dsha/config.ini", // Relative to the folder of the .nds file
    "config.ini",
    "nitro:/config.ini", // Embedded at build time (see README)
    NULL,
};

// Returns true if a config file has been loaded, false if the user has chosen
// the demo mode instead.
static bool load_config(void)
{
    char err[128] = "";
    char first_error[160] = "";

    for (int i = 0; config_paths[i] != NULL; i++)
    {
        const char *path = config_paths[i];
        if (strncmp(path, "nitro:", 6) == 0 && !nitro_ok)
            continue;

        FILE *f = fopen(path, "rb");
        if (f == NULL)
            continue;
        fclose(f);

        config_defaults(&cfg);
        if (config_load_file(&cfg, path, err, sizeof(err)))
            return true;

        // The file exists but it's wrong: report this error
        snprintf(first_error, sizeof(first_error), "%s: %s", path, err);
        break;
    }

    char body[512];
    if (first_error[0] != '\0')
    {
        snprintf(body, sizeof(body), "%s\n\nFix the file and restart DSHA.",
                 first_error);
    }
    else
    {
        snprintf(body, sizeof(body),
                 "Create the file /dsha/config.ini on your SD card:\n\n"
                 "host=192.168.1.10\n"
                 "token=<long-lived token>\n\n"
                 "See the README for all the options.%s",
                 fat_ok ? "" : "\n\n(Can't access the SD card!)");
    }

    message_wait(first_error[0] ? "Config error" : "No config file found",
                 body, "A: try the offline demo\nHold START+SELECT to exit",
                 true, KEY_A);

    // Start the demo mode
    config_defaults(&cfg);
    cfg.demo = true;
    snprintf(cfg.title, sizeof(cfg.title), "Demo house");
    return true;
}

// WiFi
// ----

static void show_status(const char *title, const char *body)
{
    app_show_message(&app, title, body, "Hold START+SELECT to exit", false,
                     true);
    present();
}

static const char *assoc_status_text(int status)
{
    switch (status)
    {
        case ASSOCSTATUS_DISCONNECTED: return "Starting...";
        case ASSOCSTATUS_SEARCHING: return "Searching for the network...";
        case ASSOCSTATUS_AUTHENTICATING: return "Authenticating...";
        case ASSOCSTATUS_ASSOCIATING: return "Associating...";
        case ASSOCSTATUS_ACQUIRINGDHCP: return "Getting an IP address (DHCP)...";
        case ASSOCSTATUS_ASSOCIATED: return "Connected!";
        case ASSOCSTATUS_CANNOTCONNECT: return "Can't connect";
        default: return "?";
    }
}

static bool find_ap(const char *ssid, Wifi_AccessPoint *out)
{
    Wifi_ScanMode();

    // Scan for a few seconds
    for (int frame = 0; frame < 60 * 4; frame++)
    {
        wait_frame();
        int count = Wifi_GetNumAP();
        for (int i = 0; i < count; i++)
        {
            Wifi_AccessPoint ap;
            if (Wifi_GetAPData(i, &ap) != WIFI_RETURN_OK)
                continue;
            if (strcmp(ap.ssid, ssid) == 0)
            {
                *out = ap;
                return true;
            }
        }
    }
    return false;
}

static bool connect_wifi(void)
{
    char body[256];

    while (1)
    {
        Wifi_EnableWifi();

        if (cfg.wifi_ssid[0] != '\0')
        {
            snprintf(body, sizeof(body), "Looking for network \"%s\"...",
                     cfg.wifi_ssid);
            show_status("Connecting to WiFi", body);

            Wifi_AccessPoint ap;
            memset(&ap, 0, sizeof(ap));
            if (!find_ap(cfg.wifi_ssid, &ap))
            {
                // It may be a hidden network: try with the name only
                memset(&ap, 0, sizeof(ap));
                snprintf(ap.ssid, sizeof(ap.ssid), "%s", cfg.wifi_ssid);
                ap.ssid_len = (u8)strlen(ap.ssid);
            }
            else if (!(ap.flags & WFLAG_APDATA_COMPATIBLE))
            {
                snprintf(body, sizeof(body),
                         "\"%s\" uses %s, which this console doesn't "
                         "support.\n\nA DS or DS Lite can only use open or "
                         "WEP networks. WPA2 needs a DSi.",
                         cfg.wifi_ssid,
                         Wifi_ApSecurityTypeString(ap.security_type));
                message_wait("Unsupported network", body, "A: retry", true,
                             KEY_A);
                continue;
            }

            Wifi_SetIP(0, 0, 0, 0, 0);
            size_t len = strlen(cfg.wifi_password);
            Wifi_ConnectSecureAP(&ap, len ? cfg.wifi_password : NULL, len);
        }
        else
        {
            show_status("Connecting to WiFi",
                        "Using the WiFi connections saved in the console "
                        "settings...");
            Wifi_AutoConnect();
        }

        // Wait up to 30 seconds
        int last = -1;
        int status = ASSOCSTATUS_DISCONNECTED;
        for (int frame = 0; frame < 60 * 30; frame++)
        {
            status = Wifi_AssocStatus();
            if (status != last)
            {
                last = status;
                snprintf(body, sizeof(body), "%s%s%s\n\nStatus: %s",
                         cfg.wifi_ssid[0] ? "Network: " : "",
                         cfg.wifi_ssid,
                         cfg.wifi_ssid[0] ? "" : "Using the console settings",
                         assoc_status_text(status));
                app_show_message(&app, "Connecting to WiFi", body,
                                 "Hold START+SELECT to exit", false, true);
            }
            if (status == ASSOCSTATUS_ASSOCIATED
                || status == ASSOCSTATUS_CANNOTCONNECT)
                break;
            wait_frame();
            if (wants_exit())
                exit(0);
        }

        if (status == ASSOCSTATUS_ASSOCIATED)
        {
            struct in_addr gw, mask, dns1, dns2;
            struct in_addr ip = Wifi_GetIPInfo(&gw, &mask, &dns1, &dns2);
            snprintf(app.ip, sizeof(app.ip), "%s", inet_ntoa(ip));
            app.wifi_quality = 3;
            return true;
        }

        Wifi_DisconnectAP();
        app.wifi_quality = 0;

        int num_wfc = Wifi_GetData(WIFIGETDATA_NUMWFCAPS, 0, NULL);
        if (cfg.wifi_ssid[0] == '\0' && num_wfc <= 0)
            snprintf(body, sizeof(body),
                     "No WiFi connections are saved in this console.\n\n"
                     "Add wifi_ssid and wifi_password to config.ini, or set "
                     "up a connection in the WiFi settings of a game.");
        else
            snprintf(body, sizeof(body),
                     "Check that the network is in range and that the "
                     "password is right.\n\n"
                     "A DS or DS Lite can only use open or WEP networks. "
                     "WPA2 needs a DSi (in DSi mode).");
        message_wait("Can't connect to WiFi", body, "A: retry", true, KEY_A);
    }
}

// Home Assistant
// --------------

static void load_progress(void *user, int done, int total)
{
    (void)user;
    static uint32_t last_draw;
    char body[160];

    // Don't spend all the time drawing
    if (platform_millis() - last_draw < 100 && done != total)
        return;
    last_draw = platform_millis();

    if (total > 0)
        snprintf(body, sizeof(body), "Server: %s:%d\n\nEntity %d of %d",
                 cfg.host, cfg.port, done, total);
    else
        snprintf(body, sizeof(body),
                 "Server: %s:%d\n\nReceived %d states (%lu KB)", cfg.host,
                 cfg.port, done,
                 (unsigned long)(ha.http.bytes_received / 1024));
    app_show_message(&app, "Loading entities", body,
                     "Hold START+SELECT to exit", false, true);
    app.frame += 8; // Animate the dots
    present();
}

static bool connect_ha(void)
{
    char body[384];

    while (1)
    {
        snprintf(body, sizeof(body), "Server: %s:%d\nDS IP: %s",
                 cfg.demo ? "demo" : cfg.host, cfg.port, app.ip);
        show_status("Connecting to Home Assistant", body);

        http_client_close(&ha.http);

        if (ha_check(&ha) && ha_load_all(&ha, &cfg, load_progress, NULL))
        {
            app_show_dashboard(&app);
            if (ha.skipped > 0)
            {
                snprintf(body, sizeof(body),
                         "%d entities not shown (limit %d). Use 'entity=' "
                         "or 'domains=' in config.ini.",
                         ha.skipped, ENTITY_MAX);
                app_toast(&app, body, false);
            }
            return true;
        }

        // Is the WiFi connection still there?
        if (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED)
        {
            Wifi_DisconnectAP();
            return false;
        }

        snprintf(body, sizeof(body),
                 "%s\n\nServer: %s:%d\n\n"
                 "Use the IP address of Home Assistant, not "
                 "homeassistant.local. HTTPS isn't supported: use the local "
                 "http:// address.",
                 ha.error, cfg.host, cfg.port);
        message_wait("Can't load Home Assistant", body, "A: retry", true,
                     KEY_A);
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    // Video: 16-bit bitmap backgrounds on both screens. The main engine is on
    // the top screen, the sub engine (touchscreen) on the bottom one.
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetPrimaryBanks(VRAM_A_MAIN_BG, VRAM_B_LCD, VRAM_C_SUB_BG, VRAM_D_LCD);
    lcdMainOnTop();

    int bg_top = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bg_bottom = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    vram_top = bgGetGfxPtr(bg_top);
    vram_bottom = bgGetGfxPtr(bg_bottom);

    irqSet(IRQ_VBLANK, vblank_handler);
    irqEnable(IRQ_VBLANK);

    app_init(&app, &cfg, &ha);
    config_defaults(&cfg);
    update_clock();

    app_show_message(&app, "Starting...", "Reading the SD card...", "", false,
                     true);
    present();

    // Holding L at boot forces DS mode WiFi even on a DSi
    scanKeys();
    bool force_ds_mode = keysHeld() & KEY_L;

    fat_ok = fatInitDefault();
    nitro_ok = nitroFSInit(NULL);

    load_config();
    ha_init(&ha, &cfg);

    if (cfg.demo)
    {
        // No network at all: everything is simulated
        snprintf(app.ip, sizeof(app.ip), "offline demo");
        connect_ha();
        while (1)
        {
            wait_frame();
            if (wants_exit())
                return 0;
            if (app.request_reload)
            {
                app.request_reload = false;
                connect_ha();
            }
        }
    }

    show_status("Starting WiFi", "Initializing the WiFi hardware...");
    if (!Wifi_InitDefault(INIT_ONLY | (force_ds_mode ? WIFI_DS_MODE_ONLY
                                                     : WIFI_ATTEMPT_DSI_MODE)))
    {
        message_wait("WiFi error", "Can't initialize the WiFi hardware.",
                     "Hold START+SELECT to exit", true, 0);
        return 0;
    }

    while (1)
    {
        if (!connect_wifi())
            continue;
        if (!connect_ha())
            continue; // The WiFi connection was lost

        // Main loop
        uint32_t frames = 0;
        while (1)
        {
            wait_frame();
            if (wants_exit())
                return 0;

            // Check the connection twice per second
            if ((++frames % 30) == 0
                && Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED)
            {
                app.wifi_quality = 0;
                http_client_close(&ha.http);
                Wifi_DisconnectAP();
                break; // Reconnect
            }

            if (app.request_reload)
            {
                app.request_reload = false;
                if (!connect_ha())
                {
                    Wifi_DisconnectAP();
                    break;
                }
            }
        }
    }

    return 0;
}
