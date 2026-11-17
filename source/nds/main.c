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

#include <sys/stat.h>

#include <mbedtls/sha256.h>

#include "app.h"
#include "config.h"
#include "discover.h"
#include "ha.h"
#include "platform.h"
#include "setup_server.h"
#include "tls.h"
#include "validate.h"

// Platform functions
// ------------------

static volatile uint32_t vblank_count;

static void vblank_handler(void)
{
    vblank_count++;
}

// The DS has no good hardware random generator (only the WiFi chip's 11-bit
// LFSR, which BlocksDS's Mbed TLS uses). DSHA keeps an entropy pool fed with
// things that can't be guessed from the network: the exact time (in 33 MHz
// timer ticks) of every network wait, of the user's key presses and touches,
// touch screen noise, the scanline being drawn and the clock. HTTPS keys mix
// both (platform_entropy() below is an extra source, see tls.c).
#define POOL_WORDS 16
static uint32_t pool[POOL_WORDS];
static unsigned pool_pos;
static uint32_t pool_adds;

static void add_entropy(uint32_t v)
{
    uint32_t x = pool[pool_pos] ^ v ^ (pool[(pool_pos + 7) % POOL_WORDS] >> 3);
    pool[pool_pos] = (x << 7) | (x >> 25);
    pool_pos = (pool_pos + 1) % POOL_WORDS;
    pool_adds++;
}

void platform_yield(void)
{
    // When network packets arrive varies by microseconds: a good source
    add_entropy(cpuGetTiming());
    cothread_yield();
}

uint32_t platform_random(void)
{
    add_entropy(REG_VCOUNT);
    add_entropy(vblank_count);
    add_entropy((uint32_t)time(NULL));
    add_entropy(cpuGetTiming());
    uint32_t x = 0;
    for (int i = 0; i < POOL_WORDS; i++)
        x = (x << 5 | x >> 27) ^ pool[i];
    // xorshift
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    add_entropy(x);
    return x;
}

void platform_entropy(unsigned char *output, size_t len)
{
    static uint32_t counter;
    size_t done = 0;
    while (done < len)
    {
        add_entropy(cpuGetTiming());
        add_entropy(REG_VCOUNT ^ (vblank_count << 9));
        add_entropy((uint32_t)time(NULL));
        counter++;

        unsigned char hash[32];
        mbedtls_sha256_context sha;
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);
        mbedtls_sha256_update(&sha, (const unsigned char *)pool, sizeof(pool));
        mbedtls_sha256_update(&sha, (const unsigned char *)&counter,
                              sizeof(counter));
        mbedtls_sha256_update(&sha, (const unsigned char *)&pool_adds,
                              sizeof(pool_adds));
        mbedtls_sha256_finish(&sha, hash);
        mbedtls_sha256_free(&sha);

        size_t n = len - done < 16 ? len - done : 16;
        // Only half of each hash is output, so the pool state can't be
        // rebuilt from what an attacker might learn
        memcpy(output + done, hash, n);
        done += n;
        memset(hash, 0, sizeof(hash));
    }
}

// The main thread's stack is in DTCM (16 KB). The TLS handshake runs in a
// cooperative thread with its own stack in main RAM instead.
int platform_run_big_stack(int (*fn)(void *), void *arg)
{
    cothread_t t = cothread_create(fn, arg, 64 * 1024, 0);
    if (t < 0)
        return fn(arg);
    while (!cothread_has_joined(t))
        cothread_yield();
    int rc = cothread_get_exit_code(t);
    cothread_delete(t);
    return rc;
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

    // Timing of key presses and touch noise feed the random generator
    if (down)
        add_entropy(down ^ REG_VCOUNT ^ (vblank_count << 8));

    if (held & KEY_TOUCH)
    {
        touchPosition touch;
        touchRead(&touch);
        add_entropy(touch.rawx | ((uint32_t)touch.rawy << 16));
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

static char config_path[64];  // File that was loaded (or "")
static char config_error[200]; // Why the file couldn't be used

typedef enum {
    CONFIG_LOADED,
    CONFIG_MISSING,
    CONFIG_INVALID,
} config_status;

static config_status load_config(void)
{
    char err[128] = "";

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
        snprintf(config_path, sizeof(config_path), "%s", path);
        if (config_load_file(&cfg, path, err, sizeof(err)))
            return CONFIG_LOADED;

        // The file exists but it's wrong
        snprintf(config_error, sizeof(config_error), "%s: %s", path, err);
        config_defaults(&cfg);
        return CONFIG_INVALID;
    }

    config_path[0] = '\0';
    return CONFIG_MISSING;
}

// Saves the config written by the setup wizard. Returns false if there is no
// writable SD card.
static bool save_config(void)
{
    // Keep using the file that was loaded, unless it's the read-only one
    // embedded in the ROM.
    if (config_path[0] != '\0' && strncmp(config_path, "nitro:", 6) != 0
        && config_save_file(&cfg, config_path))
        return true;

    static const char *const dirs[] = { "sd:/dsha", "fat:/dsha", NULL };
    for (int i = 0; dirs[i] != NULL; i++)
    {
        char path[64];
        mkdir(dirs[i], 0777);
        snprintf(path, sizeof(path), "%s/config.ini", dirs[i]);
        if (config_save_file(&cfg, path))
        {
            snprintf(config_path, sizeof(config_path), "%s", path);
            return true;
        }
    }
    return false;
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

// Waits until the connection started by Wifi_Connect*() succeeds or fails
static bool wait_for_connection(const char *network)
{
    char body[256];
    int last = -1;
    int status = ASSOCSTATUS_DISCONNECTED;

    for (int frame = 0; frame < 60 * 30; frame++)
    {
        status = Wifi_AssocStatus();
        if (status != last)
        {
            last = status;
            snprintf(body, sizeof(body), "Network: %s\n\nStatus: %s", network,
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
    return false;
}

// One attempt with the network of config.ini, or the console settings
static bool connect_configured_wifi(void)
{
    char body[256];
    Wifi_EnableWifi();

    if (cfg.wifi_ssid[0] == '\0')
    {
        show_status("Connecting to WiFi",
                    "Using the WiFi connections saved in the console "
                    "settings...");
        Wifi_AutoConnect();
        return wait_for_connection("saved in the console settings");
    }

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
        return false;
    }

    Wifi_SetIP(0, 0, 0, 0, 0);
    size_t len = strlen(cfg.wifi_password);
    Wifi_ConnectSecureAP(&ap, len ? cfg.wifi_password : NULL, len);
    return wait_for_connection(cfg.wifi_ssid);
}

// Connects with the configured network. Returns false if the user wants to
// choose another network.
static bool connect_wifi(void)
{
    char body[256];

    while (1)
    {
        if (connect_configured_wifi())
            return true;

        int num_wfc = Wifi_GetData(WIFIGETDATA_NUMWFCAPS, 0, NULL);
        if (cfg.wifi_ssid[0] == '\0' && num_wfc <= 0)
            snprintf(body, sizeof(body),
                     "No WiFi connections are saved in this console.\n\n"
                     "Press X to choose your network.");
        else
            snprintf(body, sizeof(body),
                     "Check that the network is in range and that the "
                     "password is right.\n\n"
                     "A DS or DS Lite can only use open or WEP networks. "
                     "WPA2 needs a DSi (in DSi mode).");
        uint32_t k = message_wait("Can't connect to WiFi", body,
                                  "A: retry   X: choose another network",
                                  true, KEY_A | KEY_X);
        if (k & KEY_X)
            return false; // The caller runs the WiFi picker
    }
}

// Setup wizard
// ------------

typedef enum {
    HA_CONNECTED,
    HA_WIFI_LOST,
    HA_RUN_SETUP, // Sign in again / choose another server
} ha_status;

static ha_status connect_ha(void);

// Shows app.kbd until the user presses OK (true) or goes back (false)
static bool run_keyboard(void)
{
    app_show_keyboard(&app);
    while (!app.kbd.done && !app.kbd.cancelled)
    {
        wait_frame();
        if (wants_exit())
            exit(0);
    }
    return app.kbd.done;
}

static const char *security_text(const Wifi_AccessPoint *ap)
{
    if (!(ap->flags & WFLAG_APDATA_COMPATIBLE))
        return ap->security_type == AP_SECURITY_OPEN
                   ? "not supported"
                   : "WPA - needs a DSi, not supported here";
    switch (ap->security_type)
    {
        case AP_SECURITY_OPEN: return "Open";
        case AP_SECURITY_WEP: return "WEP";
        default: return "WPA2";
    }
}

// Lets the user choose a network and type its password. Returns when the
// console is connected.
static void wifi_setup(void)
{
    static Wifi_AccessPoint aps[PICKER_MAX_ITEMS];
    static char password[65];
    char body[256];

    while (1)
    {
        Wifi_EnableWifi();
        Wifi_ScanMode();

        picker_init(&app.picker, "Choose your WiFi network",
                    "Tap your network. Networks saved in the console "
                    "connect without a password.\n\n"
                    "A DS or DS Lite can only use open and WEP networks. "
                    "WPA2 needs a DSi.");
        snprintf(app.picker.empty_text, sizeof(app.picker.empty_text),
                 "Searching for networks...");
        picker_set_actions(&app.picker, "Search again", "Hidden network");
        app_show_picker(&app);

        uint32_t frame = 0;
        while (!picker_finished(&app.picker))
        {
            // Update the list once per second
            if ((frame++ % 60) == 30)
            {
                int count = Wifi_GetNumAP();
                int n = 0;
                for (int i = 0; i < count && n < PICKER_MAX_ITEMS; i++)
                {
                    Wifi_AccessPoint ap;
                    // Skip hidden networks and names with control characters
                    // (they could be used to inject lines in config.ini)
                    if (Wifi_GetAPData(i, &ap) != WIFI_RETURN_OK
                        || ap.ssid[0] == '\0' || !valid_line(ap.ssid))
                        continue;
                    bool dup = false;
                    for (int j = 0; j < n; j++)
                        dup |= strcmp(aps[j].ssid, ap.ssid) == 0;
                    if (!dup)
                        aps[n++] = ap;
                }

                int selected = app.picker.selected;
                int scroll = app.picker.scroll;
                app.picker.count = 0;
                for (int i = 0; i < n; i++)
                {
                    char detail[48];
                    snprintf(detail, sizeof(detail), "%s%s",
                             security_text(&aps[i]),
                             (aps[i].flags & WFLAG_APDATA_CONFIG_IN_WFC)
                                 ? "  |  saved" : "");
                    picker_add(&app.picker, aps[i].ssid, detail);
                }
                app.picker.selected = selected < n ? selected : 0;
                app.picker.scroll = scroll;
                app.dirty = true;
            }
            wait_frame();
            if (wants_exit())
                exit(0);
        }

        if (app.picker.action == 0)
            continue; // Search again

        Wifi_AccessPoint ap;
        bool use_wfc = false;
        bool ask_password = true;

        if (app.picker.action == 1)
        {
            kbd_init(&app.kbd, "Network name", "", 32, KBD_TEXT, false, true);
            snprintf(app.kbd.hint, sizeof(app.kbd.hint),
                     "Type the name (SSID) of the hidden network. Upper and "
                     "lower case matter.");
            if (!run_keyboard() || app.kbd.text[0] == '\0')
                continue;
            memset(&ap, 0, sizeof(ap));
            snprintf(ap.ssid, sizeof(ap.ssid), "%s", app.kbd.text);
            ap.ssid_len = (u8)strlen(ap.ssid);
        }
        else
        {
            ap = aps[app.picker.chosen];
            if (!(ap.flags & WFLAG_APDATA_COMPATIBLE))
            {
                snprintf(body, sizeof(body),
                         "\"%s\" uses WPA/WPA2 security, which the DS and "
                         "DS Lite can't use.\n\n"
                         "Use a DSi, or create an open or WEP guest network "
                         "for the DS in your router.", ap.ssid);
                message_wait("Network not supported", body, "A: back", true,
                             KEY_A);
                continue;
            }
            use_wfc = ap.flags & WFLAG_APDATA_CONFIG_IN_WFC;
            ask_password = !use_wfc && ap.security_type != AP_SECURITY_OPEN;
        }

        password[0] = '\0';
        if (ask_password)
        {
            char title[48];
            snprintf(title, sizeof(title), "Password for \"%.20s\"",
                     ap.ssid);
            kbd_init(&app.kbd, title, "", 64, KBD_TEXT, true, true);
            snprintf(app.kbd.hint, sizeof(app.kbd.hint),
                     "Type the WiFi password%s. Press Y to show it.",
                     app.picker.action == 1 ? " (empty if it's open)" : "");
            if (!run_keyboard())
                continue;
            snprintf(password, sizeof(password), "%s", app.kbd.text);
            memset(app.kbd.text, 0, sizeof(app.kbd.text));
        }

        Wifi_SetIP(0, 0, 0, 0, 0);
        if (use_wfc)
        {
            Wifi_ConnectWfcAP(&ap);
        }
        else
        {
            size_t len = strlen(password);
            Wifi_ConnectSecureAP(&ap, len ? password : NULL, len);
        }

        if (wait_for_connection(ap.ssid))
        {
            // Remember the network unless the console already knows it
            if (use_wfc)
            {
                cfg.wifi_ssid[0] = '\0';
                cfg.wifi_password[0] = '\0';
            }
            else
            {
                snprintf(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), "%s", ap.ssid);
                snprintf(cfg.wifi_password, sizeof(cfg.wifi_password), "%s",
                         password);
            }
            return;
        }

        snprintf(body, sizeof(body),
                 "Couldn't connect to \"%s\".\n\n"
                 "Check the password (upper and lower case matter).",
                 ap.ssid);
        message_wait("Can't connect", body, "A: try again", true, KEY_A);
    }
}

// Finds Home Assistant on the network, or lets the user type its address.
// The result is "host:port".
static void find_server(char *server, size_t server_len)
{
    static discovery d;
    char body[256];

    while (1)
    {
        struct in_addr gw, mask, dns1, dns2;
        struct in_addr ip = Wifi_GetIPInfo(&gw, &mask, &dns1, &dns2);
        discovery_start(&d, ntohl(ip.s_addr), 8123);

        picker_init(&app.picker, "Looking for Home Assistant",
                    "Searching your network, this takes up to 20 seconds. "
                    "Tap your server as soon as it shows up.");
        snprintf(app.picker.empty_text, sizeof(app.picker.empty_text),
                 "Searching...");
        app.picker.progress = 0;
        picker_set_actions(&app.picker, "Type the address", "");
        app_show_picker(&app);

        int shown = 0;
        bool finished = false;
        while (!picker_finished(&app.picker))
        {
            if (!finished && !discovery_step(&d))
            {
                finished = true;
                if (d.num_found > 0)
                {
                    snprintf(app.picker.title, sizeof(app.picker.title),
                             "Found Home Assistant!");
                    snprintf(app.picker.hint, sizeof(app.picker.hint),
                             "Tap your server.");
                }
                else
                {
                    snprintf(app.picker.title, sizeof(app.picker.title),
                             "Home Assistant not found");
                    snprintf(app.picker.hint, sizeof(app.picker.hint),
                             "Make sure Home Assistant is on the same network "
                             "as the DS (guest networks are often isolated).\n"
                             "\nYou can also type its address. You can see it "
                             "in Home Assistant: Settings > System > Network.");
                    snprintf(app.picker.empty_text,
                             sizeof(app.picker.empty_text), " ");
                }
                picker_set_actions(&app.picker, "Type the address",
                                   "Search again");
                app.dirty = true;
            }

            int progress = d.total ? d.scanned * 100 / d.total : 0;
            if (progress != app.picker.progress)
            {
                app.picker.progress = progress;
                app.dirty = true;
            }
            while (shown < d.num_found)
            {
                char detail[48];
                struct in_addr a;
                a.s_addr = htonl(d.found[shown].ip);
                snprintf(detail, sizeof(detail), "%s:%d", inet_ntoa(a),
                         d.found[shown].port);
                picker_add(&app.picker, d.found[shown].host, detail);
                shown++;
                app.dirty = true;
            }

            wait_frame();
            if (wants_exit())
                exit(0);
        }
        discovery_stop(&d);

        if (app.picker.chosen >= 0)
        {
            ha_found *f = &d.found[app.picker.chosen];
            snprintf(server, server_len, "%s:%d", f->host, f->port);
            return;
        }
        if (app.picker.action == 1)
            continue; // Search again

        // Type the address. Start with the first part of the DS's IP (the
        // last address typed, if any).
        static char typed[128];
        if (typed[0] == '\0')
        {
            snprintf(typed, sizeof(typed), "%s", inet_ntoa(ip));
            char *dot = strrchr(typed, '.');
            if (dot)
                dot[1] = '\0';
        }

        kbd_init(&app.kbd, "Home Assistant address", typed, 100, KBD_URL,
                 false, true);
        snprintf(app.kbd.hint, sizeof(app.kbd.hint),
                 "Its IP address, like 192.168.1.10 (add :port if it isn't "
                 "8123), or its https:// address, like "
                 "https://xxxx.ui.nabu.casa");
        if (!run_keyboard())
            continue;
        snprintf(typed, sizeof(typed), "%s", app.kbd.text);

        char host[128];
        int port;
        bool https;
        setup_parse_server(app.kbd.text, host, sizeof(host), &port, &https);
        if (host[0] == '\0' || !valid_host(host))
        {
            message_wait("Not a valid address",
                         "Type an IP address like 192.168.1.10, or an "
                         "address like https://my.domain.", "A: try again",
                         true, KEY_A);
            continue;
        }
        static ha_config addr; // Big (entity list): not on the stack
        config_defaults(&addr);
        snprintf(addr.host, sizeof(addr.host), "%s", host);
        addr.port = port;
        addr.https = https;
        config_server_url(&addr, server, server_len);
        snprintf(body, sizeof(body), "Checking %s...%s", server,
                 https ? "\n\nThe first secure (HTTPS) connection takes a "
                         "few seconds." : "");
        show_status("Looking for Home Assistant", body);

        static http_client c;
        http_client_init_url(&c, host, port, https);
        c.timeout_ms = 4000;
        bool ok = auth_probe(&c);
        http_client_close(&c);
        if (ok)
            return;

        if (c.last_error[0] != '\0')
            snprintf(body, sizeof(body), "%s\n\nCheck the address%s.",
                     c.last_error,
                     https ? "" : " (for HTTPS, start it with https://)");
        else
            snprintf(body, sizeof(body),
                     "There's no Home Assistant at %s.\n\n"
                     "Check the address%s.", server,
                     https ? "" : " (for HTTPS, start it with https://)");
        message_wait("Not found", body, "A: try again", true, KEY_A);
    }
}

// Signs in, on the phone (web page served by the DS) or with the keyboard.
// Returns false if the user wants to choose another server.
static bool sign_in(const char *server)
{
    static setup_server srv;
    static char user[96];
    static char password[128];
    char url[48], body[256];

    bool web = setup_server_start(&srv, 80, server);
    snprintf(url, sizeof(url), "http://%s", app.ip);

    while (1)
    {
        picker_init(&app.picker, "Sign in to Home Assistant",
                    web ? "On your phone or computer (on the same WiFi), "
                          "open the address above, type the code and sign "
                          "in with your Home Assistant user.\n"
                          "Or tap \"Type on the DS\"."
                        : "Sign in with your Home Assistant user name and "
                          "password.");
        if (web)
        {
            snprintf(app.picker.big_text, sizeof(app.picker.big_text), "%s",
                     url);
            snprintf(app.picker.big_text2, sizeof(app.picker.big_text2),
                     "Code: %s", srv.pin);
            snprintf(app.picker.empty_text, sizeof(app.picker.empty_text),
                     "Waiting for you to sign in on your phone...");
        }
        else
        {
            app.picker.empty_text[0] = '\0';
        }
        picker_set_actions(&app.picker, "Type on the DS", "Back");
        app_show_picker(&app);

        while (!picker_finished(&app.picker))
        {
            setup_event ev = web ? setup_server_poll(&srv) : SETUP_NOTHING;
            if (srv.code_changed)
            {
                // A wrong code was typed (or a new one was generated)
                srv.code_changed = false;
                snprintf(app.picker.big_text2, sizeof(app.picker.big_text2),
                         "Code: %s", srv.pin);
                snprintf(app.picker.empty_text, sizeof(app.picker.empty_text),
                         "%s", srv.status);
                app.dirty = true;
            }
            if (ev == SETUP_SUBMITTED)
            {
                snprintf(app.picker.empty_text, sizeof(app.picker.empty_text),
                         "Signing in...");
                app.dirty = true;
                present();

                setup_process(&srv, &cfg);
                if (srv.state == SETUP_STATE_DONE)
                {
                    // Let the phone load the "All set!" page
                    for (int i = 0; i < 90; i++)
                    {
                        setup_server_poll(&srv);
                        wait_frame();
                    }
                    setup_server_stop(&srv);
                    return true;
                }
                snprintf(app.picker.empty_text, sizeof(app.picker.empty_text),
                         "%s", srv.state == SETUP_STATE_MFA
                                   ? "Enter the two-factor code on your phone."
                                   : srv.status);
                app.dirty = true;
            }
            wait_frame();
            if (wants_exit())
                exit(0);
        }

        if (app.picker.action == 1)
        {
            setup_server_stop(&srv);
            return false;
        }

        // Sign in with the keyboard
        kbd_init(&app.kbd, "User name", user, 60, KBD_TEXT, false, true);
        snprintf(app.kbd.hint, sizeof(app.kbd.hint),
                 "Your Home Assistant user name (not the display name). It "
                 "is usually lowercase.");
        if (!run_keyboard())
            continue;
        snprintf(user, sizeof(user), "%s", app.kbd.text);

        kbd_init(&app.kbd, "Password", "", 120, KBD_TEXT, true, true);
        snprintf(app.kbd.hint, sizeof(app.kbd.hint),
                 "Your Home Assistant password. Press Y to show it. It isn't "
                 "stored on the DS.");
        if (!run_keyboard())
            continue;
        snprintf(password, sizeof(password), "%s", app.kbd.text);
        memset(app.kbd.text, 0, sizeof(app.kbd.text));

        show_status("Signing in...", server);
        srv.state = SETUP_STATE_WAITING;
        login_result r = setup_sign_in(&srv, &cfg, server, user, password,
                                       NULL);
        memset(password, 0, sizeof(password));

        while (r == LOGIN_NEED_MFA)
        {
            kbd_init(&app.kbd, "Two-factor code", "", 8, KBD_NUMERIC, false,
                     true);
            snprintf(app.kbd.hint, sizeof(app.kbd.hint), "%s",
                     srv.status_error ? srv.status
                                      : "Enter the code from your "
                                        "authenticator app.");
            if (!run_keyboard())
                break;
            show_status("Signing in...", "Checking the code...");
            r = setup_sign_in_mfa(&srv, &cfg, app.kbd.text);
        }

        if (r == LOGIN_OK)
        {
            setup_server_stop(&srv);
            return true;
        }
        if (r == LOGIN_NEED_MFA)
            continue; // Went back from the code screen

        srv.state = SETUP_STATE_WAITING;
        snprintf(body, sizeof(body), "%s", srv.status);
        message_wait("Couldn't sign in", body, "A: try again", true, KEY_A);
    }
}

// The whole setup: WiFi (if needed), server, sign in, save
static void run_setup(bool wifi_connected)
{
    char server[160];
    char body[320];

    // The network scan needs almost all the sockets of lwIP
    http_client_close(&ha.http);

    if (!wifi_connected)
    {
        // Try the WiFi settings of the console first: often nothing else
        // is needed.
        int num_wfc = Wifi_GetData(WIFIGETDATA_NUMWFCAPS, 0, NULL);
        if (!((cfg.wifi_ssid[0] || num_wfc > 0) && connect_configured_wifi()))
            wifi_setup();
    }

    // If the server is known (signed out, for example) suggest it first
    while (1)
    {
        if (ha.signed_out && cfg.host[0] != '\0')
        {
            config_server_url(&cfg, server, sizeof(server));
            ha.signed_out = false;
        }
        else
        {
            find_server(server, sizeof(server));
        }
        if (sign_in(server))
            break;
    }

    cfg.demo = false;
    if (save_config())
        snprintf(body, sizeof(body),
                 "DSHA is connected to Home Assistant.\n\n"
                 "Your settings were saved in %s, so next time it connects "
                 "by itself.\n\n"
                 "Tip: press X in the dashboard for help and to run this "
                 "setup again.", config_path);
    else
        snprintf(body, sizeof(body),
                 "DSHA is connected to Home Assistant.\n\n"
                 "The settings couldn't be saved to the SD card, so you'll "
                 "need to sign in again next time.");
    message_wait("All set!", body, "A: open the dashboard", false, KEY_A);

    ha_init(&ha, &cfg);
}

static void run_demo(void)
{
    config_defaults(&cfg);
    cfg.demo = true;
    snprintf(cfg.title, sizeof(cfg.title), "Demo house");
    snprintf(app.ip, sizeof(app.ip), "offline demo");
    ha_init(&ha, &cfg);
    connect_ha();
    while (1)
    {
        wait_frame();
        if (wants_exit())
            exit(0);
        if (app.request_reload)
        {
            app.request_reload = false;
            connect_ha();
        }
        if (app.request_setup)
        {
            app.request_setup = false;
            app.info_visible = false;
            return; // Back to the welcome screen
        }
    }
}

// First screen when there is no config file
static bool welcome(config_status status)
{
    while (1)
    {
        picker_init(&app.picker, "Welcome to DSHA!",
                    "Control your Home Assistant from this DS.\n\n"
                    "The setup connects to WiFi, finds Home Assistant and "
                    "signs you in. It takes about a minute.");
        if (status == CONFIG_INVALID)
            snprintf(app.picker.hint, sizeof(app.picker.hint),
                     "There's a problem with your config file:\n%s\n\n"
                     "Start the setup to create a new one.", config_error);
        else if (!fat_ok)
            strncat(app.picker.hint,
                    "\n\n(The SD card can't be accessed, so the settings "
                    "won't be saved.)",
                    sizeof(app.picker.hint) - strlen(app.picker.hint) - 1);
        app.picker.empty_text[0] = '\0';
        picker_set_actions(&app.picker, "Start setup", "Try the demo");
        app_show_picker(&app);

        while (!picker_finished(&app.picker))
        {
            wait_frame();
            if (wants_exit())
                exit(0);
        }
        if (app.picker.action == 0)
            return true;
        run_demo();
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

    char server[160];
    config_server_url(&cfg, server, sizeof(server));
    if (total > 0)
        snprintf(body, sizeof(body), "Server: %s\n\nEntity %d of %d",
                 server, done, total);
    else
        snprintf(body, sizeof(body),
                 "Server: %s\n\nReceived %d states (%lu KB)", server, done,
                 (unsigned long)(ha.http.bytes_received / 1024));
    app_show_message(&app, "Loading entities", body,
                     "Hold START+SELECT to exit", false, true);
    app.frame += 8; // Animate the dots
    present();
}

static ha_status connect_ha(void)
{
    char body[384];
    char server[160];
    config_server_url(&cfg, server, sizeof(server));

    while (1)
    {
        snprintf(body, sizeof(body), "Server: %s\nDS IP: %s%s",
                 cfg.demo ? "demo" : server, app.ip,
                 cfg.https && !cfg.demo && ha.http.tls_conn == NULL
                     ? "\n\nSecure connection (HTTPS): this takes a few "
                       "seconds."
                     : "");
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
            return HA_CONNECTED;
        }

        if (cfg.demo)
            return HA_CONNECTED; // Can't fail

        // Is the WiFi connection still there?
        if (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED)
        {
            Wifi_DisconnectAP();
            return HA_WIFI_LOST;
        }

        // The session was deleted in Home Assistant: just sign in again
        if (ha.signed_out)
            return HA_RUN_SETUP;

        snprintf(body, sizeof(body),
                 "%s\n\nServer: %s\n\n%s", ha.error, server,
                 cfg.https ? "Check the address, and that Home Assistant is "
                             "reachable at it from your network."
                           : "Use the IP address of Home Assistant, not "
                             "homeassistant.local, or its https:// "
                             "address.");
        uint32_t k = message_wait("Can't load Home Assistant", body,
                                  "A: retry   X: set up again", true,
                                  KEY_A | KEY_X);
        if (k & KEY_X)
            return HA_RUN_SETUP;
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

    // Free running 33 MHz counter (timers 0 and 1), for the entropy pool
    cpuStartTiming(0);

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

    config_status status = load_config();
    bool need_setup = status != CONFIG_LOADED;

    // Extra HTTPS certificates to trust (self-signed, or a private CA)
    static const char *const ca_paths[] = {
        "sd:/dsha/ca.pem", "fat:/dsha/ca.pem", NULL,
    };
    for (int i = 0; ca_paths[i] != NULL; i++)
    {
        FILE *f = fopen(ca_paths[i], "rb");
        if (f == NULL)
            continue;
        fclose(f);
        if (!tls_set_ca_file(ca_paths[i]))
        {
            char body[160];
            snprintf(body, sizeof(body),
                     "%s isn't a valid PEM certificate file. It's ignored.",
                     ca_paths[i]);
            message_wait("Certificate file", body, "A: continue", true,
                         KEY_A);
        }
        break;
    }

    if (status == CONFIG_LOADED && cfg.demo)
        run_demo(); // Returns if the user asks for the setup
    if (need_setup || cfg.demo)
    {
        welcome(status);
        need_setup = true;
    }

    show_status("Starting WiFi", "Initializing the WiFi hardware...");
    if (!Wifi_InitDefault(INIT_ONLY | (force_ds_mode ? WIFI_DS_MODE_ONLY
                                                     : WIFI_ATTEMPT_DSI_MODE)))
    {
        message_wait("WiFi error", "Can't initialize the WiFi hardware.",
                     "Hold START+SELECT to exit", true, 0);
        return 0;
    }

    if (need_setup)
        run_setup(false);
    else
        ha_init(&ha, &cfg);

    while (1)
    {
        if (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED && !connect_wifi())
        {
            wifi_setup();
            save_config();
        }

        ha_status hs = connect_ha();
        if (hs == HA_WIFI_LOST)
            continue;
        if (hs == HA_RUN_SETUP)
        {
            run_setup(true);
            continue;
        }

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

            // Signed out while using the app (session deleted in HA)
            if (ha.signed_out)
            {
                message_wait("Signed out",
                             "Home Assistant ended the session of this DS "
                             "(it was deleted in your profile, or not used "
                             "for 90 days).\n\nPlease sign in again.",
                             "A: sign in", true, KEY_A);
                run_setup(true);
                break;
            }

            if (app.request_setup)
            {
                app.request_setup = false;
                app.info_visible = false;
                http_client_close(&ha.http);
                run_setup(true);
                break;
            }

            if (app.request_reload)
            {
                app.request_reload = false;
                hs = connect_ha();
                if (hs == HA_WIFI_LOST)
                    break;
                if (hs == HA_RUN_SETUP)
                {
                    run_setup(true);
                    break;
                }
            }
        }
    }

    return 0;
}
