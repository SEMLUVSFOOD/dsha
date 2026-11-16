# Contributing to DSHA

Thanks for helping! Bug reports, hardware test reports, docs and code are all
welcome.

## Reporting a problem

Please include:

- Console model (DS, DS Lite, DSi, 3DS...) and how you started DSHA
  (flashcart model, TWiLight Menu++, Unlaunch, emulator).
- The type of WiFi network (open/WEP/WPA2) and whether you used the console
  settings or `wifi_ssid`.
- The exact message on screen (a photo is great).
- Your Home Assistant version.

**Never post your token** (`token` or `refresh_token`). Replace it with `xxx`
if you share your config. Security problems go through a private report, see
[SECURITY.md](SECURITY.md).

Everyone taking part is expected to follow the
[code of conduct](CODE_OF_CONDUCT.md).

## Development setup

See [docs/building.md](docs/building.md). In short:

```sh
make docker   # builds dsha.nds
make test     # runs the desktop tests (gcc + python3)
```

Most of the code is in `source/core` and runs on a PC, so you can develop and
test UI and network changes without a DS. [docs/architecture.md](docs/architecture.md)
explains how the pieces fit together, including a checklist for adding a new
entity domain.

## Pull requests

- Keep the style of the surrounding code: C99, 4 spaces, braces on their own
  line, 80 columns, `snake_case`.
- Add or update tests in `tests/` for logic changes, and run `make test`.
- If you change the UI, run `make screenshots` and look at the result in
  `docs/screenshots/` (commit the updated images if the change is visible).
- Make sure `make docker` builds without warnings.
- Update the docs and `CHANGELOG.md` if users will notice the change.

By contributing you agree that your contributions are licensed under the MIT
license of the project.
