# Security policy

DSHA handles credentials for your Home Assistant installation, so security
reports are taken seriously.

## Supported versions

Only the latest release gets fixes.

## Reporting a vulnerability

**Please don't open a public issue for security problems.** Use GitHub's
private reporting instead: go to the
[Security tab](https://github.com/semluvsfood/dsha/security/advisories/new) of
the repository and click *Report a vulnerability*. Include the steps to
reproduce it and what an attacker could do. You should get an answer within a
week.

## What DSHA does with your credentials

Knowing the design helps to judge what is and isn't a vulnerability:

- **Password**: only sent to Home Assistant during sign in (login flow). It is
  never written to the SD card and is wiped from memory after use.
- **Refresh token / long-lived token**: stored in plain text in
  `/dsha/config.ini` on the SD card. Anyone with the SD card can use it. Revoke
  it from your Home Assistant profile (*Security → Refresh tokens* or
  *Long-lived access tokens*) if the card or console is lost.
- **Plain HTTP**: the DS can't do TLS at a usable speed, so DSHA talks plain
  HTTP to Home Assistant on the local network. Credentials and tokens can be
  seen by anyone who can sniff that network. WEP (the only security the DS
  and DS Lite support) is easy to break. This is a known, documented
  limitation, not a vulnerability.
- **Setup page**: during setup the DS serves a sign in page on port 80 of the
  local network. It is only active while the sign in screen is shown.

Recommendations for users are in
[docs/configuration.md](docs/configuration.md#security-notes).
