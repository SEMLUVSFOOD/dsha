# Troubleshooting

## "No config file found"

- The file must be called `config.ini` inside a folder called `dsha` in the
  root of the SD card: `/dsha/config.ini`. Watch out for `config.ini.txt`
  (Windows hides extensions by default).
- On a flashcart, the "SD card" is the flashcart's microSD card. Some old
  flashcarts need a DLDI patch for homebrew to access it; most loaders do this
  automatically. If the message says *Can't access the SD card*, try patching
  `dsha.nds` with the DLDI file of your flashcart, or
  [embed the config in the ROM](configuration.md#embedding-the-config-in-the-rom).

## "Can't connect to WiFi"

- **DS / DS Lite / 3DS**: the hardware only supports **open and WEP**
  networks. Most routers use WPA2 by default. Options: use a DSi, enable a
  separate guest network with WEP (isolate it!), or use a cheap travel router
  or a phone hotspot configured as WEP/open.
- **DSi**: WPA2 works when DSHA runs in DSi mode (from the SD card with
  TWiLight Menu++ or Unlaunch). WPA2 networks must be saved in the *Advanced
  Setup* slots (4-6) of the DSi settings, or put in `config.ini`. Holding **L**
  while DSHA starts forces DS mode (useful to rule out DSi WiFi problems).
- **WPA3-only** networks don't work on any console. Use WPA2/WPA3 mixed mode.
- If you set `wifi_ssid`, check the spelling (it's case sensitive) and the
  password.
- 5 GHz networks aren't supported: the DS only uses 2.4 GHz.

## "Can't load Home Assistant"

The message below the title says what went wrong:

| Message | Fix |
|---|---|
| `Can't resolve host ...` | Use the IP address instead of a name. `homeassistant.local` doesn't work. |
| `Timeout connecting to ...` / `Can't connect to ...` | Wrong IP or port, or the DS is on a network that can't reach Home Assistant (guest networks are often isolated). Check that `http://<host>:8123` opens in a browser of a device on the **same** network as the DS. |
| `Access denied (HTTP 401). Check the token.` | The token is wrong or was deleted. Copy it again (it is very long; check that it wasn't cut). |
| `HTTP 404: is this a Home Assistant server?` | The host/port point to something else. |
| `Server error: HTTP 400` / `HTTP 500` | Look at the Home Assistant logs. |
| `Invalid JSON from server` | Probably a proxy in between. Use the direct address of Home Assistant. |

Home Assistant may block an IP after many failed logins if `ip_ban_enabled`
is on. Check *Settings → System → Logs* and `ip_bans.yaml`.

### HTTPS / Nabu Casa / reverse proxies

DSHA only supports plain HTTP. If your Home Assistant only listens on HTTPS
(for example you configured `ssl_certificate` in `http:`), the DS can't connect.
Typical solutions: keep plain HTTP on the local network and do TLS in a reverse
proxy for remote access, or run a small HTTP-only reverse proxy on your LAN for
the DS.

## Toggling works but the tile doesn't change

Some devices take a while to report their new state. DSHA checks the entity
again 1.5 seconds after an action and then every `refresh_seconds`. Press Y to
refresh immediately.

## The entity I want isn't there

- In automatic mode only supported domains are shown, and at most 128 entities.
  Add `entity=` lines to choose exactly what you want; any domain can be shown
  that way (read only if DSHA doesn't know how to control it).
- Entity ids are lower case, like `light.living_room`. Check them in Home
  Assistant: *Settings → Entities*.

## Strange characters in names

The font covers ASCII and Latin-1 (accents like é, ü, ñ). Emoji are hidden and
other alphabets are shown as `?`. Set a different friendly name in Home
Assistant if needed.
