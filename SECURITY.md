# Security policy

DSHA handles access to your Home Assistant, which can open doors and turn off
alarms, so security reports are taken seriously.

## Supported versions

Only the latest release gets fixes.

## Reporting a vulnerability

**Please don't open a public issue for security problems.** Use GitHub's
private reporting instead: go to the
[Security tab](https://github.com/semluvsfood/dsha/security/advisories/new) of
the repository and click *Report a vulnerability*. Include the steps to
reproduce it and what an attacker could do. You should get an answer within a
week.

## Threat model

### What DSHA protects against

| Threat | Protection |
|---|---|
| **Credentials sent over the internet in clear text** (a typo in the address, a public DNS name...) | The DS refuses to connect to anything that isn't a local/private address (10/8, 172.16/12, 192.168/16, 127/8, 169.254/16, 100.64/10). This can only be disabled explicitly with `allow_public_server = 1`. |
| **Someone else on the network using the setup page** (to point your DS at their server, or sign it into their account) | The page only accepts a submission with the **6-digit code shown on the DS screen**. After 5 wrong codes a new code is generated. |
| **A malicious website open on your phone** submitting the setup form in the background (CSRF) | Same pairing code; it's never in the page. |
| **Clickjacking / content sniffing of the setup page** | `X-Frame-Options: DENY`, `Content-Security-Policy` (no scripts, `frame-ancestors 'none'`), `X-Content-Type-Options: nosniff`, `Referrer-Policy: no-referrer`, `Cache-Control: no-store`. |
| **Malicious server responses** (a rogue server, or tampering with the plain HTTP traffic) crashing the DS or corrupting memory | All parsers (HTTP, JSON, states, layout, config) work with fixed-size buffers and bounds checks. Sizes are capped. They are fuzzed with AddressSanitizer and UBSan in CI (`tests/fuzz.c`). |
| **Header / path / config injection** through values from the network: WiFi names chosen by neighbours, tokens, entity ids, flow ids | Strict validation (`source/core/validate.c`): entity ids `[a-z0-9_].[a-z0-9_]`, tokens printable ASCII without spaces, host names `[A-Za-z0-9.-_:]`. Control characters are never written to `config.ini`; WiFi networks with such names aren't listed. |
| **Password left in memory** | The password is only used for the sign in request and then wiped (request buffers included). It's never written to the SD card. |
| **Overly broad access** | The DS keeps a refresh token for one user, revocable in the Home Assistant profile. Use a non-admin user for the DS. |
| **The integration leaking states** | `/api/dsha/states` only returns entities the authenticated user may read (Home Assistant permissions). Only admins can open the panel or change the layout. |
| **XSS in the panel** (friendly names, area names are attacker-influenced) | Everything is HTML-escaped; tested in a browser with hostile names. |
| **Invalid layouts** | Validated with a strict schema (Home Assistant's entity id rules plus a full match, control characters stripped, sizes limited). |
| **Supply chain** | GitHub Actions pinned to commit hashes, no persisted credentials, minimal permissions, Playwright installed from a lockfile, Dependabot updates, CodeQL and zizmor-clean workflows. Releases are built from the tagged commit by CI. |

### Known limitations (by design)

- **Plain HTTP on the local network.** The DS can't do TLS at a usable speed,
  so DSHA talks plain HTTP to Home Assistant. Anyone who can sniff your WiFi
  can see the session token, and the password during sign in. **WEP** (the
  only security the DS and DS Lite support) is broken and offers little
  protection: if you must use it, put the DS on an isolated network that can
  only reach Home Assistant.
- **Plain text token on the SD card.** The DS has no secure storage. Anyone
  with the SD card can use the session: revoke it in your Home Assistant
  profile (*Security → Refresh tokens*) if the card or console is lost.
- **A fake Home Assistant on your network** can appear in the server list of
  the setup (discovery has no way to authenticate servers). Check that the
  address shown is your Home Assistant before signing in.
- **The setup page is single-threaded**: a device on the network can make it
  slow by opening connections and not sending anything. It's only active
  while the sign in screen is shown.
- **The pairing code** is random but the DS has no hardware random number
  generator; it mixes timers, scanline position, touch screen noise and the
  timing of key presses. It's short-lived and limited to 5 attempts.

Recommendations for users are in
[docs/configuration.md](docs/configuration.md#security-notes).
