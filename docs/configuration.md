# Configuration reference

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

If the file exists but is wrong (for example, no token), DSHA shows the error
instead of looking for other files.

## Options

| Key | Default | Description |
|---|---|---|
| `host` | *(required)* | IP address or DNS name of Home Assistant. `http://192.168.1.10:8123/` style URLs are accepted too (the port in the URL is used). `.local` names don't work. |
| `port` | `8123` | TCP port of Home Assistant. |
| `token` | *(required)* | Long-lived access token. |
| `wifi_ssid` | *(empty)* | Network to connect to. If empty, the WiFi settings saved in the console (WFC / DSi settings) are used. Hidden networks are supported. |
| `wifi_password` | *(empty)* | Password of `wifi_ssid`. Empty for open networks. WEP keys must be 5 or 13 ASCII characters. |
| `title` | `Home Assistant` | Text shown in the top bar. |
| `refresh_seconds` | `15` | How often the entities on screen are refreshed. `0` disables auto refresh (press Y to refresh). |
| `entity` | *(none)* | An entity to show. Repeat the line for more entities, or list several separated by commas. They are shown in this order. Up to 128. |
| `domains` | *(all)* | Only used when there are no `entity` lines: comma separated list of the domains to show, like `light, switch, climate`. |
| `demo` | `0` | `1` starts an offline demo with fake entities. No WiFi or server needed. |

`url`, `ssid`, `password`, `entities` and `refresh` are accepted as aliases of
`host`, `wifi_ssid`, `wifi_password`, `entity` and `refresh_seconds`. A line
with only an entity id (like `light.kitchen`) is treated as an `entity` line.

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

- The token gives full access to your Home Assistant account. Create a
  dedicated token for the DS (so you can revoke it any time) and consider a
  dedicated non-admin user for it.
- The connection is plain HTTP, so anyone able to sniff your WiFi network can
  see the token. WEP is easy to break: if you use a WEP or open network for
  the DS, isolate it from the rest of your network.
