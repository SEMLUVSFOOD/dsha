# How DSHA works

```
source/
├── core/               Portable C99 code: no DS-specific calls
│   ├── app.c / app.h   Application state, input handling, refresh scheduling
│   ├── ui.c            Drawing of both screens
│   ├── gfx.c           Software renderer for 256x192 BGR555 framebuffers
│   ├── font8x8.c       8x8 bitmap font (ASCII + Latin-1)
│   ├── ha.c            Home Assistant REST API: states and service calls
│   ├── entity.c        Entity model, per-domain actions and state formatting
│   ├── http.c          HTTP/1.1 client (keep-alive, chunked) over BSD sockets
│   ├── json_stream.c   Streaming (SAX) JSON parser
│   ├── config.c        config.ini parser
│   ├── demo_data.c     Fake states for the offline demo
│   └── platform.h      What a platform must provide (yield, milliseconds)
└── nds/
    └── main.c          DS entry point: video, input, WiFi, SD card, main loop
tests/                  Desktop tests, fake Home Assistant, screenshot renderer
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

**Rendering.** Both screens use 16-bit bitmap backgrounds (mode 5, BG3). The UI
is drawn into buffers in main RAM only when something changed, then copied to
VRAM with DMA. Text is drawn with an 8x8 font at a 7 pixel advance (36
characters per line), scaled 2x or 3x for titles and values.

**One request per frame.** Actions and refreshes are queued; `app_update()`
does at most one network request per frame, and only after the screen has
been redrawn. That way, pressing A immediately shows the tile as "..." before
the request blocks for a moment.

## Home Assistant API usage

| Request | When |
|---|---|
| `GET /api/` | At start: checks address and token |
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
