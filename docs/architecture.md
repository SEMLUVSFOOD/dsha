# How DSHA works

```
source/
├── core/               Portable C99 code: no DS-specific calls
│   ├── app.c / app.h   Application state, input handling, refresh scheduling
│   ├── ui.c            Drawing of both screens
│   ├── gfx.c           Software renderer for 256x192 BGR555 framebuffers
│   ├── font8x8.c       8x8 bitmap font (ASCII + Latin-1)
│   ├── ha.c            Home Assistant REST API: states and service calls
│   ├── auth.c          Sign in with user/password (login flow) and tokens
│   ├── discover.c      Finds Home Assistant in the local network
│   ├── setup_server.c  Sign in web page served by the DS during setup
│   ├── widgets.c       On-screen keyboard and list picker for the setup
│   ├── entity.c        Entity model, per-domain actions and state formatting
│   ├── http.c          HTTP/1.1 client (keep-alive, chunked) over BSD sockets
│   ├── tls.c           HTTPS: TLS connections with Mbed TLS
│   ├── ca_roots.c      Trusted root certificates (tools/gen_ca_roots.py)
│   ├── json_stream.c   Streaming (SAX) JSON parser
│   ├── json_get.c      Picks values by path ("errors.base") from small JSON
│   ├── validate.c      Validation of untrusted values (ids, hosts, tokens)
│   ├── config.c        config.ini parser
│   ├── demo_data.c     Fake states for the offline demo
│   └── platform.h      What a platform must provide (yield, time, entropy)
└── nds/
    └── main.c          DS entry point: video, input, WiFi, SD card, main loop
custom_components/dsha/ Home Assistant integration (Python)
├── __init__.py         Panel registration, websocket and REST API
├── layout.py           Layout validation and storage
├── config_flow.py      "Add integration" dialog
└── frontend/dsha-panel.js  The DS Dashboard panel (web component, no build)
tests/                  Desktop tests, fake Home Assistant, screenshot renderer
tests/fuzz.c            Fuzzer of all the parsers (ASan + UBSan)
tests/panel/            Browser test of the panel (Playwright)
tests_ha/               Integration tests inside a real Home Assistant core
```

## Design choices

**Portable core.** Everything except `source/nds/main.c` compiles on a PC.
The tests link the same files with `tests/host_platform.c`, and the
screenshots in this repository are produced by the real UI code
(`tests/render.c`) running against `tests/fake_ha.py`.

**Streaming JSON.** `GET /api/states` returns every entity with all its
attributes: easily several megabytes on a real installation, while the DS has
4 MB of RAM in total. `json_stream.c` is an incremental parser that emits
events (keys, values, start/end of objects) as bytes arrive, with a fixed
256-byte string buffer. `ha.c` listens to those events and only keeps the
fields it needs for each entity (`ha_entity` is ~300 bytes).

**HTTP client.** Requests use HTTP/1.1 with keep-alive, so refreshing a page of
8 entities reuses one TCP connection. If the server closed an idle connection,
the request is retried once on a new one. Bodies with `Content-Length`,
chunked encoding or delimited by the end of the connection are supported. All
socket operations are non-blocking: while waiting, `platform_yield()` lets the
lwIP thread of DSWifi run (on the DS it calls `cothread_yield()`), and
timeouts are measured with a VBlank counter.

`poll()` is used instead of `select()` on purpose: lwIP on the DS uses its
own `fd_set` layout, which doesn't match the one of the C library.

**HTTPS.** With an `https://` address, `http.c` sends and receives through
`tls.c`, a thin layer over Mbed TLS 3.6 (BlocksDS's `blocksds-mbedtls`
package) on the same non-blocking socket. The handshake is the slow part on
a DS: it runs in a cooperative thread with a 64 KB stack in main RAM
(`platform_run_big_stack()`; the main thread's stack is 16 KB of DTCM), and
X25519 is preferred for the key exchange because it's the fastest. After the
handshake the connection is kept alive like a plain one. Certificates are
verified against `ca_roots.c` (generated from Mozilla's CA store by
`tools/gen_ca_roots.py`, kept in the ROM without copying) and
`/dsha/ca.pem`. Over plain HTTP, non-local servers are refused; over HTTPS
they're allowed.

**Rendering.** Both screens use 16-bit bitmap backgrounds (mode 5, BG3). The UI
is drawn into buffers in main RAM only when something changed, then copied to
VRAM with DMA. Text is drawn with an 8x8 font at a 7 pixel advance (36
characters per line), scaled 2x or 3x for titles and values.

**One request per frame.** Actions and refreshes are queued; `app_update()`
does at most one network request per frame, and only after the screen has
been redrawn. That way, pressing A immediately shows the tile as "..." before
the request blocks for a moment.

## Setup wizard

`source/nds/main.c` runs the wizard when there is no config file:

1. **WiFi**: the networks saved in the console are tried first
   (`Wifi_AutoConnect()`). Otherwise a list of networks is shown, refreshed
   every second while DSWifi scans, and the password is typed with the
   on-screen keyboard (`widgets.c`).
2. **Server discovery** (`discover.c`): the DS has no mDNS, so it resolves a
   few host names routers commonly register (`homeassistant`,
   `homeassistant.lan`...), then scans the /24 subnet for port 8123 with 6
   non-blocking connections at a time (lwIP on the DS has 8 sockets). Each
   open port is confirmed with `GET /auth/providers`, which only Home
   Assistant answers that way and doesn't need authentication.
3. **Sign in** (`auth.c`): the same login flow the Home Assistant apps use:
   `POST /auth/login_flow` with DSHA's client id
   (`https://github.com/semluvsfood/dsha`), then the user name and password
   (and a two-factor code if requested), then `POST /auth/token` to exchange
   the authorization code for an access token and a refresh token. The
   redirect URI has the same host as the client id, so Home Assistant accepts
   it without fetching anything.
4. **Phone page** (`setup_server.c`): while the sign in screen is shown, the
   DS listens on port 80 and serves a small form. Submitting it runs the same
   sign in code (`setup_process()`), and the page shows the progress
   (two-factor code, errors, done).
5. The config is written with `config_save_file()`. Only the refresh token is
   stored, never the password.

Access tokens last 30 minutes. `ha_http()` in `ha.c` gets a new one from the
refresh token when a request returns 401 and repeats the request. Because the
HTTP client only passes the body of 2xx responses to callbacks, the retry
doesn't confuse the streaming parsers.

## DS Dashboard layouts

With the integration installed, `ha_load_all()` first requests
`/api/dsha/layout`. A 404 means it isn't installed and the normal modes are
used. Otherwise the layout is streamed into `ha_client.pages` (16 pages of 8
slots holding entity indices, -1 for empty slots), custom names and tap
actions are stored in the entities (`custom_name`, `tile_action`) and survive
state updates, and the states come from `/api/dsha/states` in one request.
The app's list is then the slots of all pages in order, so the existing
paging, touch and drawing code works unchanged; empty slots are drawn as an
outline and have no actions. Page refreshes use `/api/dsha/states?page=N`.

The panel (`dsha-panel.js`) is a plain custom element: Home Assistant passes
it the `hass` object, it reads/writes the layout with websocket commands and
renders with `innerHTML`. Drag and drop uses pointer events (mouse and touch)
and `shadowRoot.elementFromPoint()` to find the drop target. State changes
only update the state texts, so typing and dragging aren't disturbed.

## Home Assistant API usage

| Request | When |
|---|---|
| `GET /auth/providers` | Setup: confirms that a host is Home Assistant |
| `POST /auth/login_flow`, `POST /auth/login_flow/<id>` | Setup: sign in (and two-factor code) |
| `POST /auth/token` | Exchange the sign in code for tokens; renew the access token |
| `GET /api/` | At start: checks address and token |
| `GET /api/dsha/layout` | At start/reload: the DS Dashboard layout (404 = not installed) |
| `GET /api/dsha/states[?page=N]` | Layout mode: states of all/one page in one request |
| `GET /api/states` | At start/reload in automatic mode |
| `GET /api/states/<entity_id>` | At start in manual mode, page refreshes, 1.5 s after an action |
| `POST /api/services/<domain>/<service>` | Actions. The response (list of changed states) is applied right away |

The services used per domain are in `entity_build_service()` in
`source/core/entity.c`.

## Adding support for a new domain

1. Add it to `entity_domain` in `entity.h` and to the `domains` table in
   `entity.c` (name, label, shown in automatic mode, sort order).
2. Add its actions to `entity_action_label()` and `entity_build_service()`.
3. If it has interesting attributes, parse them in `handle_attribute()`
   (`ha.c`) and show them in `draw_entity_details()` (`ui.c`).
4. Put it in a filter group in `filter_match()` (`app.c`).
5. Add a state to `tests/fixtures/states.json`, a test in
   `tests/test_core.c`, and support in `tests/fake_ha.py` if it has actions.
