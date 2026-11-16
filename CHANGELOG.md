# Changelog

## 1.1.0 — 2026-11-16

Setup is now as easy as we could make it: no config file or token needed.

- First-run setup wizard.
- WiFi network list with an on-screen touch keyboard for the password (the
  networks saved in the console are still tried first).
- Automatic discovery of Home Assistant in the local network.
- Sign in with your Home Assistant user name and password, from your phone
  (the DS serves a sign in page) or with the DS keyboard. Two-factor
  authentication supported. Long-lived tokens can still be used.
- Sessions are renewed automatically; a clear "Signed out" message if the
  session is revoked.
- The settings are saved to `/dsha/config.ini` by the wizard. Run it again
  from the info panel (X, then A).
- Demo mode available from the welcome screen.

## 1.0.0 — 2026-11-16

First release.

- Native Nintendo DS app built with BlocksDS (DS, DS Lite, DSi, 3DS).
- WiFi using the console's saved connections or `wifi_ssid`/`wifi_password`
  (WPA2 on DSi in DSi mode).
- Touch dashboard with 8 tiles per page, filters, details on the top screen.
- Lights (toggle, brightness), switches, input booleans, fans, automations,
  sirens, humidifiers, climate (on/off, target temperature), covers, locks,
  media players (play/pause, volume), vacuums, scenes, scripts, buttons,
  numbers, sensors, binary sensors, weather and people.
- Automatic mode (all supported entities) or a hand picked entity list.
- Streaming JSON parser and keep-alive HTTP client.
- Auto refresh of the visible entities.
- Offline demo mode.
- Desktop test suite with a fake Home Assistant server.
