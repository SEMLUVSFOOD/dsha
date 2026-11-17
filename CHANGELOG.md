# Changelog

## 1.3.0 — 2026-11-17

- **HTTPS.** Sign in to Home Assistant at an `https://` address: Nabu Casa
  remote access (`https://xxxx.ui.nabu.casa`), DuckDNS, your own domain or a
  reverse proxy, or Home Assistant itself with `ssl_certificate`. TLS 1.2 and
  1.3 with Mbed TLS. The certificate is always verified, against the root
  certificates of the common authorities (Let's Encrypt, Google Trust
  Services, Sectigo/ZeroSSL, DigiCert, Amazon, GlobalSign, GoDaddy, SSL.com,
  Microsoft) and, optionally, your own in `/dsha/ca.pem` (self-signed
  certificates, private CAs).
- **New address keyboard** in the setup: letters, `:` and `/` on the same
  page, an `https://` key and a *Clear* key. It used to only allow digits.
- The phone sign in page accepts `https://` addresses too, and both show why
  a server couldn't be reached (certificate problem, not HTTPS, timeout...).
- `https = 1` in `config.ini` (written by the setup), or `host =
  https://...`. `https://` addresses use port 443 unless another one is given.
- Non-local servers are allowed over HTTPS (they're still refused over plain
  HTTP).
- The first HTTPS connection takes a few seconds on a DS; it's kept open
  afterwards. The handshake runs on its own 64 KB stack.
- Better randomness for the keys: an entropy pool of network and input
  timings, mixed with Mbed TLS's own source.

### Development

- Desktop tests now run the whole network suite over HTTPS as well (TLS 1.3
  and 1.2, EC and RSA certificates, wrong name, unknown authority), with
  AddressSanitizer and UBSan. They need Mbed TLS 3.6: `tests/get_mbedtls.sh`
  builds it.
- The build needs BlocksDS's `blocksds-mbedtls` package (`make docker`
  installs it).
- Releases check that the tag matches the version, and use the changelog as
  release notes.

## 1.2.0 — 2026-11-16

- New **Home Assistant integration** (`custom_components/dsha`, installable
  with HACS) that adds a **DS Dashboard** panel to the sidebar:
  - Drag and drop entities, automations, scripts and scenes into the DS's
    pages (8 tiles per page, empty slots allowed), with mouse or touch.
  - Rename tiles, choose what a tap does (default, trigger automation, read
    only), reorder and rename pages, create pages from areas.
- The DS uses the layout automatically when the integration is installed:
  page names, custom names, "Run now" for automations, and one request per
  page refresh.
- `use_layout = 0` in `config.ini` ignores the layout.
- The demo shows a layout with three pages.

### Security

- Fixed an out of bounds read in the HTTP client with huge chunk sizes
  (found by the new fuzzer). Content-Length is validated too.
- The setup page needs the 6-digit pairing code shown on the DS (protects
  against other devices on the network and CSRF), with a new code after 5
  wrong attempts, and sends security headers.
- DSHA refuses servers outside the local network (no credentials over the
  internet in clear text); `allow_public_server = 1` to override.
- Strict validation of entity ids, host names and tokens from the network;
  control characters (from WiFi names, for example) are never written to
  `config.ini`.
- Passwords are wiped from memory after signing in.
- Integration: strict entity id validation (a trailing newline was accepted
  by Home Assistant's own check), control characters stripped from names,
  states filtered by user permissions (tested).
- New fuzz tests (parsers and hostile clients of the setup page), XSS test of
  the panel, CodeQL, pinned GitHub Actions, Dependabot, zizmor-clean
  workflows.

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
