// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Unit tests of the portable core (no network needed).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "config.h"
#include "entity.h"
#include "gfx.h"
#include "ha.h"
#include "json_get.h"
#include "json_stream.h"
#include "testlib.h"

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

// JSON
// ----

static char json_log[4096];

static void log_cb(void *user, json_event ev, const char *value, int depth)
{
    (void)user;
    static const char *names[] = { "{", "}", "[", "]", "K", "S", "N", "T",
                                   "F", "0" };
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s%d%s%s ", names[ev], depth,
             value ? ":" : "", value ? value : "");
    strncat(json_log, tmp, sizeof(json_log) - strlen(json_log) - 1);
}

static void test_json(void)
{
    const char *doc = "{\"a\": [1, -2.5e3, true, false, null],"
                      " \"b\\\"c\": \"x\\u00e9\\n\\ud83d\\ude00\","
                      " \"o\": {}, \"e\": []}";
    const char *expected =
        "{0 K1:a [1 N2:1 N2:-2.5e3 T2:true F2:false 02:null ]1 "
        "K1:b\"c S1:x\xC3\xA9\n\xF0\x9F\x98\x80 K1:o {1 }1 K1:e [1 ]1 }0 ";

    // Feed the document with different chunk sizes
    for (size_t chunk = 1; chunk <= strlen(doc); chunk++)
    {
        json_parser p;
        json_log[0] = '\0';
        json_init(&p, log_cb, NULL);
        bool ok = true;
        for (size_t i = 0; i < strlen(doc); i += chunk)
        {
            size_t n = strlen(doc) - i < chunk ? strlen(doc) - i : chunk;
            ok = ok && json_feed(&p, doc + i, n);
        }
        ok = ok && json_finish(&p);
        CHECK(ok);
        if (strcmp(json_log, expected) != 0)
        {
            printf("  chunk %zu:\n  got: %s\n  exp: %s\n", chunk, json_log,
                   expected);
            CHECK(0);
            break;
        }
    }

    // Errors
    const char *bad[] = { "{\"a\" 1}", "[1,]x", "{]", "tru", "[\"a\"}" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        json_parser p;
        json_init(&p, NULL, NULL);
        bool ok = json_feed(&p, bad[i], strlen(bad[i])) && json_finish(&p);
        CHECK(!ok);
    }

    // Top level number
    json_parser p;
    json_log[0] = '\0';
    json_init(&p, log_cb, NULL);
    CHECK(json_feed(&p, "42", 2));
    CHECK(json_finish(&p));
    CHECK_STR(json_log, "N0:42 ");
}

// Config
// ------

static void test_config(void)
{
    ha_config cfg;
    char err[128];

    config_defaults(&cfg);
    const char *text =
        "\xEF\xBB\xBF# comment\n"
        "[homeassistant]\n"
        "host = http://192.168.1.20:8124/\r\n"
        "token = \"abc.def.ghi\"\n"
        "wifi_ssid = My Net\n"
        "wifi_password = pass word\n"
        "refresh_seconds = 30\n"
        "title = Sam's house\n"
        "entity = light.Kitchen\n"
        "entities = switch.a, switch.b ,\n"
        "sensor.temp\n"
        "unknown_key = 1\n";
    CHECK(config_parse(&cfg, text, err, sizeof(err)));
    CHECK_STR(cfg.host, "192.168.1.20");
    CHECK(cfg.port == 8124);
    CHECK_STR(cfg.token, "abc.def.ghi");
    CHECK_STR(cfg.wifi_ssid, "My Net");
    CHECK_STR(cfg.wifi_password, "pass word");
    CHECK(cfg.refresh_seconds == 30);
    CHECK_STR(cfg.title, "Sam's house");
    CHECK(cfg.num_entities == 4);
    CHECK_STR(cfg.entities[0], "light.kitchen");
    CHECK_STR(cfg.entities[2], "switch.b");
    CHECK_STR(cfg.entities[3], "sensor.temp");

    config_defaults(&cfg);
    CHECK(!config_parse(&cfg, "host=1.2.3.4\n", err, sizeof(err)));
    CHECK(strstr(err, "token") != NULL);

    config_defaults(&cfg);
    CHECK(config_parse(&cfg, "host=ha.lan\ntoken=x\nport=80\n"
                             "domains=light, switch\n", err, sizeof(err)));
    CHECK(cfg.port == 80);
    CHECK(config_domain_allowed(&cfg, "light.a"));
    CHECK(config_domain_allowed(&cfg, "switch.a"));
    CHECK(!config_domain_allowed(&cfg, "sensor.a"));
    CHECK(!config_domain_allowed(&cfg, "lightx.a"));
}

// Entities and the states parser
// ------------------------------

static ha_entity *find(ha_client *ha, const char *id)
{
    for (int i = 0; i < ha->count; i++)
    {
        if (strcmp(ha->entities[i].id, id) == 0)
            return &ha->entities[i];
    }
    return NULL;
}

static void test_states(void)
{
    static ha_client ha;
    static ha_config cfg;
    char err[64];
    size_t len;
    char *doc = read_file("fixtures/states.json", &len);

    config_defaults(&cfg);
    CHECK(config_parse(&cfg, "host=x\ntoken=y\n", err, sizeof(err)));
    ha_init(&ha, &cfg);
    CHECK(ha.auto_mode);

    // Feed in odd-sized chunks
    ha_state_parser *sp = ha_parser_begin(&ha, &cfg, true);
    for (size_t i = 0; i < len; i += 97)
        CHECK(ha_parser_feed(sp, doc + i, len - i < 97 ? len - i : 97));
    CHECK(ha_parser_end(sp));
    ha_sort(&ha);

    // sun, update and zone aren't supported
    CHECK(ha.count == 22);
    CHECK(find(&ha, "sun.sun") == NULL);
    CHECK(find(&ha, "zone.home") == NULL);
    CHECK(ha.entities[0].domain == DOM_LIGHT);
    CHECK(ha.entities[ha.count - 1].domain == DOM_SENSOR);

    char s[64];
    ha_entity *e = find(&ha, "light.living_room");
    CHECK(e != NULL && e->dimmable && e->brightness == 191.0f);
    CHECK_STR(e->name, "Living room ceiling");
    entity_format_state(e, s, sizeof(s));
    CHECK_STR(s, "On 75%");

    e = find(&ha, "light.hallway");
    CHECK(e != NULL && !e->dimmable);
    CHECK(!entity_has_action(e, ACT_DECREASE));
    entity_format_state(e, s, sizeof(s));
    CHECK_STR(s, "Off");

    e = find(&ha, "light.k\xC3\xBC" "che");
    CHECK(e != NULL && e->dimmable && e->brightness == ATTR_NONE);
    CHECK_STR(e->name, "K\xC3\xBC" "che \xC3\xA9" "clairage \xE2\x9C\xA8");

    e = find(&ha, "climate.thermostat");
    CHECK(e != NULL);
    entity_format_state(e, s, sizeof(s));
    CHECK_STR(s, "Heat 20.5\xC2\xB0");
    CHECK_STR(e->extra, "heating");

    char dom[32], svc[32], body[192];
    CHECK(entity_build_service(e, ACT_INCREASE, dom, sizeof(dom), svc,
                               sizeof(svc), body, sizeof(body)));
    CHECK_STR(dom, "climate");
    CHECK_STR(svc, "set_temperature");
    CHECK_STR(body, "{\"entity_id\":\"climate.thermostat\",\"temperature\":22}");

    e = find(&ha, "sensor.outside_temperature");
    entity_format_state(e, s, sizeof(s));
    CHECK_STR(s, "7.85 \xC2\xB0" "C");

    e = find(&ha, "sensor.humidity");
    entity_format_state(e, s, sizeof(s));
    CHECK_STR(s, "64%");

    e = find(&ha, "binary_sensor.back_door");
    entity_format_state(e, s, sizeof(s));
    CHECK_STR(s, "Open");

    e = find(&ha, "binary_sensor.hall_motion");
    entity_format_state(e, s, sizeof(s));
    CHECK_STR(s, "Clear");

    e = find(&ha, "person.sam");
    entity_format_state(e, s, sizeof(s));
    CHECK_STR(s, "Home");

    e = find(&ha, "weather.home");
    entity_format_state(e, s, sizeof(s));
    CHECK_STR(s, "Partly cloudy 8.2\xC2\xB0" "C");

    e = find(&ha, "media_player.kitchen_speaker");
    CHECK_STR(e->extra, "Bohemian \"Rhapsody\"");
    CHECK(e->volume > 0.34f && e->volume < 0.36f);

    e = find(&ha, "fan.bedroom");
    CHECK(entity_is_unavailable(e));
    CHECK(!entity_has_action(e, ACT_PRIMARY));

    e = find(&ha, "lock.front_door");
    CHECK(entity_build_service(e, ACT_PRIMARY, dom, sizeof(dom), svc,
                               sizeof(svc), body, sizeof(body)));
    CHECK_STR(svc, "unlock");

    e = find(&ha, "input_number.target");
    CHECK(entity_build_service(e, ACT_DECREASE, dom, sizeof(dom), svc,
                               sizeof(svc), body, sizeof(body)));
    CHECK_STR(body, "{\"entity_id\":\"input_number.target\",\"value\":41}");

    e = find(&ha, "vacuum.robot");
    CHECK(entity_build_service(e, ACT_PRIMARY, dom, sizeof(dom), svc,
                               sizeof(svc), body, sizeof(body)));
    CHECK_STR(svc, "start");

    e = find(&ha, "light.living_room");
    CHECK(entity_build_service(e, ACT_DECREASE, dom, sizeof(dom), svc,
                               sizeof(svc), body, sizeof(body)));
    CHECK_STR(svc, "turn_on");
    CHECK_STR(body, "{\"entity_id\":\"light.living_room\","
                    "\"brightness_step_pct\":-10}");

    // Single object update (as returned by /api/states/<id>)
    sp = ha_parser_begin(&ha, NULL, false);
    const char *one = "{\"entity_id\":\"switch.coffee_machine\",\"state\":"
                      "\"off\",\"attributes\":{}}";
    CHECK(ha_parser_feed(sp, one, strlen(one)));
    CHECK(ha_parser_end(sp));
    e = find(&ha, "switch.coffee_machine");
    CHECK_STR(e->state, "off");
    // Without friendly_name, the name is derived from the entity id
    CHECK_STR(e->name, "Coffee machine");

    free(doc);
}

// Manual mode keeps the configured order and ignores other entities
static void test_manual_mode(void)
{
    static ha_client ha;
    static ha_config cfg;
    char err[64];
    size_t len;
    char *doc = read_file("fixtures/states.json", &len);

    config_defaults(&cfg);
    CHECK(config_parse(&cfg, "host=x\ntoken=y\nentity=sensor.humidity\n"
                             "entity=light.hallway\nentity=light.missing\n",
                       err, sizeof(err)));
    ha_init(&ha, &cfg);
    CHECK(!ha.auto_mode);
    CHECK(ha.count == 3);

    ha_state_parser *sp = ha_parser_begin(&ha, &cfg, false);
    CHECK(ha_parser_feed(sp, doc, len));
    CHECK(ha_parser_end(sp));

    CHECK(ha.count == 3);
    CHECK_STR(ha.entities[0].id, "sensor.humidity");
    CHECK_STR(ha.entities[0].name, "Humidity");
    CHECK(ha.entities[0].loaded);
    CHECK_STR(ha.entities[1].name, "Hallway");
    CHECK(!ha.entities[2].loaded);
    CHECK_STR(ha.entities[2].name, "Missing");

    free(doc);
}

static void test_utf8(void)
{
    const char *s = "a\xC3\xA9\xE2\x9C\xA8\xF0\x9F\x98\x80\xFF";
    CHECK(utf8_next(&s) == 'a');
    CHECK(utf8_next(&s) == 0xE9);
    CHECK(utf8_next(&s) == 0x2728);
    CHECK(utf8_next(&s) == 0x1F600);
    CHECK(utf8_next(&s) == 0xFFFD);
    CHECK(utf8_next(&s) == 0);
    CHECK(gfx_text_width("h\xC3\xA9", 1) == 2 * GFX_CHAR_W);
    // Emoji are skipped
    CHECK(gfx_text_width("a\xE2\x9C\xA8\xF0\x9F\x98\x80" "b", 1) == 2 * GFX_CHAR_W);
}

static void test_demo(void)
{
    static ha_client ha;
    static ha_config cfg;
    char err[64];

    config_defaults(&cfg);
    CHECK(config_parse(&cfg, "demo=1\n", err, sizeof(err)));
    CHECK(cfg.demo);
    ha_init(&ha, &cfg);
    CHECK(ha_check(&ha));
    CHECK(ha_load_all(&ha, &cfg, NULL, NULL));

    // The demo uses a layout like the one of the DS Dashboard panel
    CHECK(ha.layout_mode);
    CHECK(ha.count == 19);
    CHECK(ha.num_pages == 3);
    CHECK_STR(ha.pages[0].name, "Living room");
    CHECK_STR(ha.pages[2].name, "Security");
    CHECK(ha.pages[0].slots[5] == -1);     // Empty slot
    CHECK(ha.pages[1].slots[4] == -1);
    CHECK_STR(ha.layout_title, "Demo house");

    ha_entity *e = find(&ha, "light.living_room");
    CHECK_STR(e->name, "Ceiling");         // Custom name...
    CHECK(e->brightness == 191.0f);        // ...and the real state

    char dom[32], svc[32], body[192];
    e = find(&ha, "automation.lights_at_sunset");
    CHECK(e->tile_action == TILE_ACTION_TRIGGER);
    CHECK_STR(entity_action_label(e, ACT_PRIMARY), "Run now");
    CHECK(entity_build_service(e, ACT_PRIMARY, dom, sizeof(dom), svc,
                               sizeof(svc), body, sizeof(body)));
    CHECK_STR(svc, "trigger");
    e = find(&ha, "person.sam");
    CHECK(!entity_has_action(e, ACT_PRIMARY)); // Read only

    e = find(&ha, "switch.coffee_machine");
    CHECK(ha_action(&ha, e, ACT_PRIMARY));
    CHECK_STR(e->state, "off");
    e = find(&ha, "light.living_room");
    CHECK(ha_action(&ha, e, ACT_INCREASE));
    CHECK(e->brightness > 191.0f);
    e = find(&ha, "lock.front_door");
    CHECK(ha_action(&ha, e, ACT_PRIMARY));
    CHECK_STR(e->state, "unlocked");
    CHECK(ha.http.requests == 0);

    // Without the layout: all the supported entities
    static ha_client ha2;
    config_defaults(&cfg);
    CHECK(config_parse(&cfg, "demo=1\nuse_layout=0\n", err, sizeof(err)));
    ha_init(&ha2, &cfg);
    CHECK(ha_load_all(&ha2, &cfg, NULL, NULL));
    CHECK(!ha2.layout_mode);
    CHECK(ha2.count == 22);
}

// Touch handling of the dashboard, with simulated time
static void touch_frame(app_t *app, bool touching, int x, int y, uint32_t *t)
{
    app_input in = { 0 };
    in.touching = touching;
    in.tx = x;
    in.ty = y;
    *t += 17;
    app_update(app, &in, *t);
    app->dirty = false; // As if it had been drawn
}

static void test_touch(void)
{
    static ha_client ha;
    static ha_config cfg;
    static app_t app;
    char err[64];
    uint32_t t = 1000;

    config_defaults(&cfg);
    CHECK(config_parse(&cfg, "demo=1\nrefresh_seconds=0\n", err, sizeof(err)));
    ha_init(&ha, &cfg);
    CHECK(ha_load_all(&ha, &cfg, NULL, NULL));
    app_init(&app, &cfg, &ha);
    app.now_ms = t;
    app_show_dashboard(&app);

    // Holding the "next page" button for many frames changes page only once
    for (int i = 0; i < 20; i++)
        touch_frame(&app, true, 230, 178, &t);
    touch_frame(&app, false, 0, 0, &t);
    CHECK(app.selected / TILES_PER_PAGE == 1);
    CHECK(app.queued_entity == -1);

    // Back to page 1
    touch_frame(&app, true, 10, 178, &t);
    touch_frame(&app, false, 0, 0, &t);
    CHECK(app.selected / TILES_PER_PAGE == 0);

    // Short tap on the 3rd tile (living room light): selects and toggles
    touch_frame(&app, true, 60, 60, &t);
    touch_frame(&app, true, 61, 61, &t);
    touch_frame(&app, false, 0, 0, &t);
    CHECK(app.selected == 2);
    CHECK(app.queued_entity == app.list[2]);
    touch_frame(&app, false, 0, 0, &t); // Runs the action
    CHECK(app.queued_entity == -1);
    CHECK_STR(ha.entities[app.list[2]].state, "paused"); // The speaker

    // Tapping an empty slot of the layout only selects it
    touch_frame(&app, true, 190, 100, &t);
    touch_frame(&app, false, 0, 0, &t);
    CHECK(app.selected == 5);
    CHECK(app.list[5] == -1);
    CHECK(app.queued_entity == -1);
    CHECK_STR(app_page_title(&app), "Living room");

    // SELECT goes to the next page of the layout
    {
        app_input in = { 0 };
        in.down = IN_SELECT;
        t += 17;
        app_update(&app, &in, t);
        CHECK_STR(app_page_title(&app), "Climate");
        in.down = IN_SELECT;
        t += 17;
        app_update(&app, &in, t);
        in.down = IN_SELECT;
        t += 17;
        app_update(&app, &in, t);
        CHECK_STR(app_page_title(&app), "Living room");
    }

    // Long press on the 1st tile only selects it
    for (int i = 0; i < 40; i++)
        touch_frame(&app, true, 20, 10, &t);
    touch_frame(&app, false, 0, 0, &t);
    CHECK(app.selected == 0);
    CHECK(app.queued_entity == -1);

    // A missed release followed by a new touch elsewhere: the new position
    // is used, not the old one
    touch_frame(&app, true, 20, 10, &t);
    t += 1000; // Blocked for a second, release not seen
    touch_frame(&app, true, 190, 10, &t);
    touch_frame(&app, false, 0, 0, &t);
    CHECK(app.selected == 1);
}

static void test_json_get(void)
{
    char a[32], b[32], c[32], d[32];
    json_field f[] = {
        { "type", a, sizeof(a), false },
        { "errors.base", b, sizeof(b), false },
        { "providers.1.id", c, sizeof(c), false },
        { "missing", d, sizeof(d), false },
    };
    CHECK(json_get("{\"x\":[1,{\"type\":\"no\"}],\"type\":\"form\","
                   "\"errors\":{\"base\":\"invalid_auth\"},"
                   "\"providers\":[{\"id\":null},{\"id\":\"abc\"}]}", f, 4));
    CHECK_STR(a, "form");
    CHECK_STR(b, "invalid_auth");
    CHECK_STR(c, "abc");
    CHECK(!f[3].found && d[0] == '\0');

    // Top level arrays
    json_field g[] = { { "0.type", a, sizeof(a), false } };
    CHECK(json_get("[{\"type\":\"homeassistant\"}]", g, 1));
    CHECK_STR(a, "homeassistant");
}

static void test_config_write(void)
{
    static ha_config a, b;
    static char buf[4096];
    char err[64];

    config_defaults(&a);
    snprintf(a.host, sizeof(a.host), "10.0.0.5");
    a.port = 8124;
    snprintf(a.refresh_token, sizeof(a.refresh_token), "ref-123");
    snprintf(a.wifi_ssid, sizeof(a.wifi_ssid), "Net");
    snprintf(a.wifi_password, sizeof(a.wifi_password), "p w#d");
    snprintf(a.entities[0], CFG_ENTITY_ID_LEN, "light.a");
    a.num_entities = 1;
    a.refresh_seconds = 20;

    config_write(&a, buf, sizeof(buf));
    config_defaults(&b);
    CHECK(config_parse(&b, buf, err, sizeof(err)));
    CHECK_STR(b.host, "10.0.0.5");
    CHECK(b.port == 8124);
    CHECK_STR(b.refresh_token, "ref-123");
    CHECK_STR(b.wifi_ssid, "Net");
    CHECK_STR(b.wifi_password, "p w#d");
    CHECK(b.num_entities == 1);
    CHECK(b.refresh_seconds == 20);
}

static void tap(keyboard *k, int key)
{
    // Tap the middle of the key with the touch screen
    for (int y = 0; y < 192; y++)
    {
        for (int x = 0; x < 256; x++)
        {
            if (kbd_key_at(k, x, y) == key)
            {
                app_input in = { 0 };
                in.touching = true;
                in.tx = x + 3;
                in.ty = y + 3;
                kbd_update(k, &in);
                in.touching = false;
                kbd_update(k, &in);
                return;
            }
        }
    }
    CHECK(0); // Key not found
}

static void test_keyboard(void)
{
    static keyboard k;
    kbd_init(&k, "User name", "", 16, KBD_TEXT, false, true);

    tap(&k, 10 + 5); // y
    CHECK_STR(k.text, "y");
    tap(&k, 40);     // Shift (once)
    tap(&k, 10 + 5); // Y
    tap(&k, 10 + 5); // y
    CHECK_STR(k.text, "yYy");
    tap(&k, 41);     // Symbols
    tap(&k, 0);      // !
    tap(&k, 30 + 5); // é
    CHECK_STR(k.text, "yYy!\xC3\xA9");
    tap(&k, 43);     // Backspace removes the whole UTF-8 character
    CHECK_STR(k.text, "yYy!");
    tap(&k, 42);     // Space
    CHECK_STR(k.text, "yYy! ");

    // Buttons: B deletes, D-pad + A types, START finishes
    app_input in = { 0 };
    in.down = IN_B;
    kbd_update(&k, &in);
    CHECK_STR(k.text, "yYy!");
    in.down = IN_A; // Cursor starts on "q" (row 1, col 0), symbols layer: '-'
    kbd_update(&k, &in);
    CHECK_STR(k.text, "yYy!-");

    // Max length
    for (int i = 0; i < 20; i++)
        tap(&k, 0);
    CHECK(strlen(k.text) == 16);

    CHECK(!k.done);
    in.down = IN_START;
    kbd_update(&k, &in);
    CHECK(k.done);

    // Numeric keypad
    kbd_init(&k, "Code", "", 6, KBD_NUMERIC, false, false);
    tap(&k, 0);  // 1
    tap(&k, 10); // 0
    tap(&k, 9);  // .
    tap(&k, 11); // :
    CHECK_STR(k.text, "10.:");
    tap(&k, 44); // OK
    CHECK(k.done);
}

static void test_picker(void)
{
    static picker p;
    char name[16];
    picker_init(&p, "Choose", "hint");
    for (int i = 0; i < 6; i++)
    {
        snprintf(name, sizeof(name), "Item %d", i);
        picker_add(&p, name, "detail");
    }
    picker_set_actions(&p, "Rescan", "Other");

    app_input in = { 0 };
    // Scroll down with the arrow and tap the first visible row
    in.touching = true; in.tx = 240; in.ty = 120;
    picker_update(&p, &in);
    in.touching = false;
    picker_update(&p, &in);
    CHECK(p.scroll == 1);
    in.touching = true; in.tx = 50; in.ty = 10;
    picker_update(&p, &in);
    in.touching = false;
    picker_update(&p, &in);
    CHECK(p.chosen == 1);

    // Action buttons
    picker_init(&p, "Choose", "");
    picker_set_actions(&p, "Rescan", "Other");
    in.touching = true; in.tx = 200; in.ty = 170;
    picker_update(&p, &in);
    in.touching = false;
    picker_update(&p, &in);
    CHECK(p.action == 1);
    CHECK(picker_finished(&p));

    // D-pad and A
    picker_init(&p, "Choose", "");
    picker_add(&p, "a", "");
    picker_add(&p, "b", "");
    in.down = IN_DOWN;
    picker_update(&p, &in);
    in.down = IN_A;
    picker_update(&p, &in);
    CHECK(p.chosen == 1);
}

int main(void)
{
    test_json();
    test_config();
    test_states();
    test_manual_mode();
    test_utf8();
    test_demo();
    test_touch();
    test_json_get();
    test_config_write();
    test_keyboard();
    test_picker();
    TEST_REPORT("test_core");
}
