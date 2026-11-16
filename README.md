# DSHA — Home Assistant for the Nintendo DS

Control your smart home from a Nintendo DS. DSHA is a native `.nds` homebrew
app that talks directly to your [Home Assistant](https://www.home-assistant.io/)
server over WiFi: toggle lights, dim them, change the thermostat, open the
garage, lock the door, run scenes and read your sensors — with the touch screen
or the buttons.

<p align="center">
  <img src="docs/screenshots/02_dashboard.png" width="256" alt="Dashboard">
  <img src="docs/screenshots/04_climate.png" width="256" alt="Thermostat">
  <img src="docs/screenshots/05_sensors.png" width="256" alt="Sensors">
</p>

- **Native DS app**: written in C with [BlocksDS](https://blocksds.skylyrac.net),
  runs on real hardware (DS, DS Lite, DSi, 3DS) and emulators.
- **Touch dashboard**: 8 tiles per page, tap to toggle, colors show what's on.
- **Big details view** on the top screen with the relevant controls for each
  entity type.
- **Works with big installations**: the JSON parser streams the response, so
  thousands of entities don't need to fit in the DS's 4 MB of RAM.
- **Auto refresh** of the visible entities, keep-alive HTTP connections.
- **Easy setup**: a wizard finds your WiFi and your Home Assistant server,
  and you sign in from your phone (or the DS keyboard) with your normal
  account. No tokens to copy, no files to edit.
- **Offline demo mode** to try it without a server.
- **Open source** (MIT), with desktop unit tests and a fake Home Assistant
  server for development.

## Contents

- [Requirements](#requirements)
- [Quick start](#quick-start)
- [Controls](#controls)
- [Supported entities](#supported-entities)
- [Configuration](docs/configuration.md)
- [Troubleshooting](docs/troubleshooting.md)
- [Building from source](docs/building.md)
- [How it works](docs/architecture.md)
- [Contributing](CONTRIBUTING.md)

## Requirements

**Console** — any of these:

| Console | WiFi security supported | Notes |
|---|---|---|
| DS / DS Lite | Open, WEP | Needs a flashcart (R4, Acekard, DSTT...). Most home routers use WPA2, so you may need a separate open/WEP guest network or a travel router. |
| DSi / DSi XL | Open, WEP, **WPA2** | Run it from the SD card with [TWiLight Menu++](https://github.com/DS-Homebrew/TWiLightMenu) or Unlaunch (DSi mode). |
| 3DS / 2DS | Open, WEP (WPA2 in DSi mode: untested) | Run it with TWiLight Menu++. Reports welcome! |
| Emulators | — | melonDS (with WiFi emulation) or the offline demo in any emulator. |

**Home Assistant** — any recent version, reachable on your local network over
plain HTTP (the default `http://<ip>:8123`). You sign in with your normal Home
Assistant user name and password; no token or config file needed.

## Quick start

**1. Copy `dsha.nds` to your SD card.** Download it from the
[Releases page](https://github.com/semluvsfood/dsha/releases) (or
[build it yourself](docs/building.md)).

**2. Start it** from your flashcart menu or TWiLight Menu++ and tap
**Start setup**. The wizard does the rest:

| | |
|---|---|
| <img src="docs/screenshots/09_setup_welcome.png" width="200"> | **WiFi.** DSHA first tries the WiFi settings saved in the console. If there are none, it lists the networks around you: tap yours and type the password on the touch keyboard. |
| <img src="docs/screenshots/12_setup_found.png" width="200"> | **Find Home Assistant.** DSHA searches your network and lists the Home Assistant servers it finds. Tap yours. (You can also type the IP address.) |
| <img src="docs/screenshots/13_setup_signin.png" width="200"> | **Sign in.** The DS shows an address like `http://192.168.1.57`. Open it on your **phone or computer** and sign in with your Home Assistant user name and password, like on the Home Assistant website. Prefer the DS? Tap **Type on the DS** and use the keyboard. Two-factor codes are supported. |

<p align="center">
  <img src="docs/screenshots/phone_signin.png" width="240" alt="Sign in page on a phone">
</p>

**3. Done.** The settings are saved to `/dsha/config.ini` on the SD card, so
next time DSHA goes straight to your dashboard. Your password isn't stored:
DSHA keeps a session (refresh token) that you can revoke any time in your Home
Assistant profile → *Security* → *Refresh tokens*.

> **Tips**
> - No server yet? Tap **Try the demo** on the welcome screen.
> - To sign in with another account or server later, press **X** in the
>   dashboard and then **A**.
> - Prefer config files or long-lived tokens? That still works, see the
>   [configuration reference](docs/configuration.md). You can also paste a
>   token in the phone sign in page.

## Controls

| Input | Action |
|---|---|
| **Tap a tile** | Toggle / run it (lights, switches, scenes...) |
| **Hold a tile** | Select it without toggling |
| **D-pad** | Move the selection (left/right at the edge changes page) |
| **A** | Main action of the selected entity (toggle, lock/unlock, play/pause...) |
| **L / R** | Decrease / increase: brightness, temperature, volume, cover close/open, number value |
| **Y** or ⟳ button | Refresh the entities on screen now |
| **SELECT** or the middle button | Filter: All, Lights, Switches, Climate, Covers & locks, Media, Scenes & scripts, Sensors |
| **◀ / ▶ buttons** | Previous / next page |
| **X** | Info panel (server, IP, traffic, help). Press **A** there to sign in again or change server |
| **START** | Reload all entities |
| **START + SELECT** (hold) | Exit |

The bottom of the top screen always shows what A, L and R do for the selected
entity.

## Supported entities

| Domain | Shows | A | L / R |
|---|---|---|---|
| `light` | On/off and brightness | Toggle | Dim / brighten (10% steps) |
| `switch`, `input_boolean`, `fan`, `automation`, `siren`, `humidifier` | On/off | Toggle | — |
| `climate` | Mode and current temperature, target, action | On/off | Target temperature −/+ |
| `cover` | Open/closed and position | Toggle | Close / open |
| `lock` | Locked/unlocked | Lock or unlock | — |
| `media_player` | State, title, volume | Play/pause | Volume −/+ |
| `vacuum` | State | Start / return to base | — |
| `scene`, `script` | — | Run | — |
| `button`, `input_button` | — | Press | — |
| `number`, `input_number` | Value and range | — | Value −/+ |
| `sensor`, `binary_sensor`, `weather`, `person` | State with units, friendly texts (Open/Closed, Detected/Clear...) | — | — |

Entities from other domains are shown (read only) when you list them with
`entity=` lines.

## Limitations

- **No HTTPS.** TLS on a 67 MHz CPU is slow and memory hungry, so DSHA talks
  plain HTTP. Use the local address of Home Assistant, not your Nabu Casa or
  other remote `https://` URL. Only use DSHA on a network you trust: the
  sign in and the session token travel unencrypted (as with any `http://`
  access to Home Assistant).
- **WPA2 only on DSi.** The original DS WiFi hardware only supports open and
  WEP networks. If your router is WPA2-only, use a DSi, or create an open/WEP
  guest network (ideally isolated, with only Home Assistant reachable).
- **No `.local` names.** mDNS isn't supported; use the IP address (normal DNS
  names work).
- At most 128 entities are shown. Use `entity=` or `domains=` to choose.
- The display is refreshed by polling (not the WebSocket API), only for the
  entities on screen.

## Project status

The app has been tested with the desktop test suite (HTTP client, streaming
parser, sign in, network discovery, phone setup page and UI logic against a
fake Home Assistant server) and the real `.nds` has been run in an emulator
(dashboard in demo mode, setup wizard screens and keyboard). The WiFi parts
haven't been tried on real hardware yet. See the
[changelog](CHANGELOG.md). Reports from real hardware are very welcome:
please [open an issue](https://github.com/semluvsfood/dsha/issues) with your
console model and how you launched DSHA.

<p align="center">
  <img src="docs/screenshots/emulator_welcome.png" width="256" alt="Setup running in DeSmuME">
  <img src="docs/screenshots/emulator_tap.png" width="256" alt="Running in DeSmuME">
  <img src="docs/screenshots/03_brightness.png" width="256" alt="Dimming a light">
</p>

## License

DSHA is released under the [MIT license](LICENSE). The ROM includes libraries
from BlocksDS (libnds, DSWifi with lwIP and Mbed TLS) under their own
licenses; see [licenses/](licenses/README.md).

DSHA is not affiliated with Nintendo or with the Home Assistant project.
"Nintendo DS" is a trademark of Nintendo. "Home Assistant" is a trademark of
the Open Home Foundation.
