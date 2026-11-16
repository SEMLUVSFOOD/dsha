# Configuration reference

**You normally don't need this page**: the setup wizard (shown when there is
no config file) connects to WiFi, finds Home Assistant, signs you in and
writes `/dsha/config.ini` for you. Edit the file if you want to fine tune
things like the list of entities. To run the wizard again, press **X** in the
dashboard and then **A**, or delete the file.

DSHA reads a plain text file with `key = value` lines. Lines starting with `#`
or `;` are comments. Keys are case insensitive, values can optionally be
quoted. Unknown keys are ignored.

A commented example is in [`config.example.ini`](../config.example.ini).

## Where DSHA looks for the file

The first file found is used:

| Path | When |
|---|---|
| `sd:/dsha/config.ini` | DSi / 3DS SD card (DSi mode) |
| `fat:/dsha/config.ini` | Flashcart microSD card |
| `sd:/dsha.ini`, `fat:/dsha.ini` | Alternative location in the root |
| `dsha/config.ini`, `config.ini` | Relative to the folder of `dsha.nds` (when the loader passes it) |
| `nitro:/config.ini` | Embedded in the ROM at build time (see below) |

If the file exists but is wrong (for example, no token), DSHA shows the
problem on the welcome screen and offers to run the setup, which writes a new
file. The wizard saves to the file that was loaded, or to `sd:/dsha/config.ini`
(DSi SD card) or `fat:/dsha/config.ini` (flashcart).

## Options

| Key | Default | Description |
|---|---|---|
| `host` | *(required)* | IP address or DNS name of Home Assistant. `http://192.168.1.10:8123/` style URLs are accepted too (the port in the URL is used). `.local` names don't work. |
| `port` | `8123` | TCP port of Home Assistant. |
| `token` | *(see below)* | Long-lived access token. |
| `refresh_token` | *(see below)* | Session created by signing in with the setup wizard. Written by DSHA; you can revoke it in your Home Assistant profile (*Security → Refresh tokens*, listed as `https://github.com/semluvsfood/dsha`). |
| `wifi_ssid` | *(empty)* | Network to connect to. If empty, the WiFi settings saved in the console (WFC / DSi settings) are used. Hidden networks are supported. |
| `wifi_password` | *(empty)* | Password of `wifi_ssid`. Empty for open networks. WEP keys must be 5 or 13 ASCII characters. |
| `title` | `Home Assistant` | Text shown in the top bar. |
| `refresh_seconds` | `15` | How often the entities on screen are refreshed. `0` disables auto refresh (press Y to refresh). |
| `entity` | *(none)* | An entity to show. Repeat the line for more entities, or list several separated by commas. They are shown in this order. Up to 128. |
| `domains` | *(all)* | Only used when there are no `entity` lines: comma separated list of the domains to show, like `light, switch, climate`. |
| `use_layout` | `1` | Use the layout of the DS Dashboard panel when the [Home Assistant integration](integration.md) is installed. `0` ignores it. |
| `demo` | `0` | `1` starts an offline demo with fake entities. No WiFi or server needed. |

Either `token` or `refresh_token` is required (the wizard writes
`refresh_token`; a long-lived token pasted in the phone page is written as
`token`).

`url`, `ssid`, `password`, `entities` and `refresh` are accepted as aliases of
`host`, `wifi_ssid`, `wifi_password`, `entity` and `refresh_seconds`. A line
with only an entity id (like `light.kitchen`) is treated as an `entity` line.

## Which entities are shown

1. If the [DSHA integration](integration.md) is installed in Home Assistant and
   its layout has at least one tile: **that layout** (unless `use_layout = 0`).
2. Otherwise, if there are `entity` lines: **that list**.
3. Otherwise: **all supported entities** (automatic mode).

## Automatic mode vs entity list

**Without `entity` lines** (automatic mode), DSHA downloads `/api/states` once
at start (and when you press START) and keeps every supported entity, sorted
by type and name. The download is streamed, so it works with any number of
entities, but with thousands of entities it can take a while over the DS's
slow WiFi. Only the first 128 supported entities are kept; use `domains` to
narrow it down.

**With `entity` lines**, DSHA requests each entity separately
(`/api/states/<entity_id>`), which is much faster on big installations, and
shows them in your order. Entities that don't exist are shown as unavailable.

Either way, while the dashboard is open only the entities of the current page
are refreshed, one request at a time, so the app stays responsive.

## Embedding the config in the ROM

If you prefer not to keep the file on the SD card (or for an emulator), put
your config in `nitrofs/config.ini` before building:

```sh
mkdir -p nitrofs
cp config.example.ini nitrofs/config.ini   # and edit it
make docker
```

The file is stored inside `dsha.nds`. **Don't share a ROM built like this**:
it contains your token. `nitrofs/config.ini` is in `.gitignore` so it isn't
committed by accident. A config on the SD card has priority over the embedded
one.

## Security notes

- The token or session in `config.ini` gives access to your Home Assistant
  account: treat the SD card like a key. You can revoke it any time from your
  Home Assistant profile. Consider a dedicated non-admin user for the DS.
- Your password is only used to sign in and is never written to the SD card.
- During setup, the DS serves the sign in page on port 80 of your local
  network. It stops as soon as you are signed in.
- The connection is plain HTTP, so anyone able to sniff your WiFi network can
  see the token. WEP is easy to break: if you use a WEP or open network for
  the DS, isolate it from the rest of your network.
