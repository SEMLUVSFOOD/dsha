# DSHA — Home Assistant for the Nintendo DS

[![CI](https://github.com/semluvsfood/dsha/actions/workflows/ci.yml/badge.svg)](https://github.com/semluvsfood/dsha/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform: Nintendo DS](https://img.shields.io/badge/platform-Nintendo%20DS%20%7C%20DSi%20%7C%203DS-red.svg)](#requirements)

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
- **Your own layout**: an optional Home Assistant integration adds a
  *DS Dashboard* page to the sidebar where you drag and drop entities,
  automations and scenes into the DS's pages, exactly how you want them.
- **Offline demo mode** to try it without a server.
- **HTTPS too**: connect to the local `http://` address, or to an
  `https://` address like your Nabu Casa remote URL or your own domain. The
  certificate is always verified.
- **Security first**: plain HTTP only on the local network, pairing code for
  the setup page, password never stored, hardened and fuzzed parsers. See
  [SECURITY.md](SECURITY.md).
- **Open source** (MIT), with desktop unit tests and a fake Home Assistant
  server for development.

## Contents

- [Requirements](#requirements)
- [Quick start](#quick-start)
- [Controls](#controls)
- [Supported entities](#supported-entities)
- [Arrange your dashboard in Home Assistant](#arrange-your-dashboard-in-home-assistant)
- [Configuration](docs/configuration.md)
- [Troubleshooting](docs/troubleshooting.md)
- [Building from source](docs/building.md)
- [How it works](docs/architecture.md)
- [Contributing](CONTRIBUTING.md) · [Code of conduct](CODE_OF_CONDUCT.md) · [Security](SECURITY.md)

## Requirements

**Console** — any of these:

| Console | WiFi security supported | Notes |
|---|---|---|
| DS / DS Lite | Open, WEP | Needs a flashcart (R4, Acekard, DSTT...). Most home routers use WPA2, so you may need a separate open/WEP guest network or a travel router. |
| DSi / DSi XL | Open, WEP, **WPA2** | Run it from the SD card with [TWiLight Menu++](https://github.com/DS-Homebrew/TWiLightMenu) or Unlaunch (DSi mode). |
| 3DS / 2DS | Open, WEP (WPA2 in DSi mode: untested) | Run it with TWiLight Menu++. Reports welcome! |
| Emulators | — | melonDS (with WiFi emulation) or the offline demo in any emulator. |

**Home Assistant** — any recent version, reachable from the DS's network:
either on the local network over plain HTTP (the default `http://<ip>:8123`),
or at an `https://` address (Nabu Casa remote access, DuckDNS, a reverse
proxy...). You sign in with your normal Home Assistant user name and password;
no token or config file needed.

## Quick start

**1. Copy `dsha.nds` to your SD card.** Download it from the
[latest release](https://github.com/semluvsfood/dsha/releases/latest) (or
[build it yourself](docs/building.md)).

**2. Start it** from your flashcart menu or TWiLight Menu++ and tap
**Start setup**. The wizard does the rest:

| | |
|---|---|
| <img src="docs/screenshots/09_setup_welcome.png" width="200"> | **WiFi.** DSHA first tries the WiFi settings saved in the console. If there are none, it lists the networks around you: tap yours and type the password on the touch keyboard. |
| <img src="docs/screenshots/12_setup_found.png" width="200"> | **Find Home Assistant.** DSHA searches your network and lists the Home Assistant servers it finds. Tap yours. Or tap *Type the address* and enter its IP, or an `https://` address like `https://xxxx.ui.nabu.casa` (the keyboard has an `https://` key). |
| <img src="docs/screenshots/13_setup_signin.png" width="200"> | **Sign in.** The DS shows an address like `http://192.168.1.57` and a 6-digit code. Open the address on your **phone or computer**, type the code and sign in with your Home Assistant user name and password, like on the Home Assistant website. Prefer the DS? Tap **Type on the DS** and use the keyboard. Two-factor codes are supported. |

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

## Arrange your dashboard in Home Assistant

Out of the box the DS shows all your supported entities, grouped by type. To
choose exactly what goes where, install the **DSHA integration** for Home
Assistant (via HACS or by hand). It adds a **DS Dashboard** page to the
sidebar: drag entities, automations, scripts and scenes into the DS's pages,
rename tiles, choose what a tap does, or create pages from your areas. Press
**Save**, then **START** on the DS.

<p align="center">
  <img src="docs/screenshots/ha_panel.png" width="560" alt="DS Dashboard panel in Home Assistant">
</p>
<p align="center">
  <img src="docs/screenshots/15_layout_living_room.png" width="220" alt="Layout on the DS">
  <img src="docs/screenshots/16_layout_security.png" width="220" alt="Another page">
</p>

Installation and details: **[docs/integration.md](docs/integration.md)**.

## Controls

| Input | Action |
|---|---|
| **Tap a tile** | Toggle / run it (lights, switches, scenes...) |
| **Hold a tile** | Select it without toggling |
| **D-pad** | Move the selection (left/right at the edge changes page) |
| **A** | Main action of the selected entity (toggle, lock/unlock, play/pause...) |
| **L / R** | Decrease / increase: brightness, temperature, volume, cover close/open, number value |
| **Y** or ⟳ button | Refresh the entities on screen now |
| **SELECT** or the middle button | With a DS Dashboard layout: next page. Otherwise filter: All, Lights, Switches, Climate, Covers & locks, Media, Scenes & scripts, Sensors |
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
| `switch`, `input_boolean`, `fan`, `automation`, `siren`, `humidifier` | On/off | Toggle (automations: or *Run now*, set in the DS Dashboard panel) | — |
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

- **HTTPS is slow to start.** The first secure connection takes a few
  seconds of maths on a 67 MHz DS (less on a DSi); after that the connection
  is kept open and it's as fast as HTTP. Certificates from the common
  authorities (Let's Encrypt, Google, Sectigo/ZeroSSL, DigiCert...) work out
  of the box; for a self-signed certificate, copy it to `/dsha/ca.pem` (see
  [configuration](docs/configuration.md#https)).
- **Plain HTTP is local only.** Over `http://`, the sign in and the session
  token travel unencrypted, so DSHA refuses non-local `http://` addresses. Use
  `https://` for anything outside your network. Details in
  [SECURITY.md](SECURITY.md).
- **WPA2 only on DSi.** The original DS WiFi hardware only supports open and
  WEP networks. If your router is WPA2-only, use a DSi, or create an open/WEP
  guest network (ideally isolated, with only Home Assistant reachable).
- **No `.local` names.** mDNS isn't supported; use the IP address (normal DNS
  names work).
- At most 128 entities are shown (16 pages with the integration). Use the
  DS Dashboard panel, `entity=` or `domains=` to choose.
- The display is refreshed by polling (not the WebSocket API), only for the
  entities on screen.

## Project status

The app has been tested with the desktop test suite (HTTP and HTTPS client,
streaming parser, sign in, network discovery, phone setup page and UI logic
against a fake Home Assistant server) and the real `.nds` has been run in an emulator
(dashboard and layout pages in demo mode, setup wizard screens and keyboard).
The Home Assistant integration is tested inside a real Home Assistant core,
and its panel in a browser (drag and drop included). The WiFi parts and
HTTPS haven't been tried on real hardware yet. See the
[changelog](CHANGELOG.md). Reports from real hardware are very welcome:
please [open an issue](https://github.com/semluvsfood/dsha/issues) with your
console model and how you launched DSHA.

<p align="center">
  <img src="docs/screenshots/emulator_welcome.png" width="256" alt="Setup running in DeSmuME">
  <img src="docs/screenshots/emulator_layout.png" width="256" alt="A layout page running in DeSmuME">
  <img src="docs/screenshots/03_brightness.png" width="256" alt="Dimming a light">
</p>

## Contributing

Bug reports, hardware test reports and pull requests are welcome — see
[CONTRIBUTING.md](CONTRIBUTING.md). Please report security problems privately
as described in [SECURITY.md](SECURITY.md).

## License

DSHA (the DS app and the Home Assistant integration) is released under the
[MIT license](LICENSE). The ROM includes libraries
from BlocksDS (libnds, DSWifi with lwIP, Mbed TLS) and root certificates from
Mozilla's CA store under their own licenses; see
[licenses/](licenses/README.md).

DSHA is not affiliated with Nintendo or with the Home Assistant project.
"Nintendo DS" is a trademark of Nintendo. "Home Assistant" is a trademark of
the Open Home Foundation.
