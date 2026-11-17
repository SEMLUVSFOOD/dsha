# Building DSHA

DSHA is built with [BlocksDS](https://blocksds.skylyrac.net), an open source
SDK for the Nintendo DS. The result is `dsha.nds`.

## Option 1: Docker (easiest)

With Docker installed, from the root of the repository:

```sh
make docker
```

This runs the build inside the official `skylyrac/blocksds:slim-v1.25.0`
image, after installing BlocksDS's Mbed TLS package in it (used for HTTPS).
If you don't have `make` either:

```sh
docker run --rm -v "$PWD":/work -w /work skylyrac/blocksds:slim-v1.25.0 \
    sh -c "wf-pacman -Sy --noconfirm blocksds-mbedtls && make"
```

## Option 2: BlocksDS installed locally

Follow the [BlocksDS setup guide](https://blocksds.skylyrac.net/docs/setup/)
(it uses the Wonderful toolchain on Linux/macOS/Windows with MSYS2), then
install Mbed TLS and build:

```sh
wf-pacman -S blocksds-mbedtls
make
```

The Makefile uses `$BLOCKSDS` (default `/opt/blocksds/core`).

## Desktop tests

The core of the app (HTTP client, JSON parser, Home Assistant API, UI logic
and rendering) is portable C, so it can be tested on a PC. You need `gcc`,
`make`, `python3`, `openssl` (to make test certificates), Mbed TLS 3.6 and,
for screenshots, `Pillow`. Mbed TLS: `libmbedtls-dev` on Debian 13 / Ubuntu
25.04 or newer, `brew install mbedtls@3` on macOS, or
`sh tests/get_mbedtls.sh` anywhere (downloads and builds it in
`tests/build/mbedtls`; CI uses this).

```sh
make test          # unit tests + network tests against tests/fake_ha.py
make screenshots   # renders docs/screenshots/*.png with the real UI code
```

Besides the unit tests, the suite signs in (with and without two-factor
authentication), renews expired sessions, scans 127.0.0.0/24 to discover the
fake server, and drives the phone sign in page like a browser would.

The network tests run three times, with the fake server answering with
`Content-Length`, with chunked transfer encoding and with
`Connection: close`, to cover the different ways an HTTP server can frame a
response. Then they run again over HTTPS against the same server with test
certificates: TLS 1.3 and 1.2, EC and RSA certificates, keep-alive and a new
connection per request, plus a certificate for the wrong name, an unknown
authority, and HTTP/HTTPS mix-ups. The tests are built with AddressSanitizer
and UBSan.

## Trying it in an emulator

- **Any emulator**: put `demo=1` in `nitrofs/config.ini`, build, and open
  `dsha.nds`. Or start it without config and press A.
- **melonDS with WiFi**: melonDS can emulate WiFi. Start the fake server
  (`python3 tests/fake_ha.py --bind 0.0.0.0`), embed a config pointing to your
  PC's LAN IP with `token=testtoken`, and enable WiFi in melonDS. You may need
  the DS firmware for WiFi settings. In the setup, sign in with user `sam`
  and password `secret` (user `mfa` also asks for the code `123456`).

## Debugging

`tests/fake_ha.py` prints every request it receives, which helps to see what
the DS is doing. Run it with `--mode chunked` or `--mode close` to try the
other response formats.

The DS build can be made with the debug versions of the libraries by changing
`LIBS` in the Makefile to `-ldswifi9d -lnds9d` and `ARM7ELF` to
`arm7_dswifi_debug.elf`; debug messages then go to the no$gba console.

## Releasing

Pushing a tag like `v1.0.1` makes the GitHub Actions workflow build the ROM and
attach `dsha.nds`, `config.example.ini` and the licenses to a GitHub release.
Remember to update `DSHA_VERSION` in `source/core/app.h` and the
[changelog](../CHANGELOG.md) first.
