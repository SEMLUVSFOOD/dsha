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
make docker            # builds dsha.nds
make test              # desktop tests of the DS app (gcc + python3)
make test-integration  # Home Assistant integration tests (see below)
make test-panel        # browser test of the DS Dashboard panel (node + Playwright)
```

The integration tests need a Python 3.13 virtual environment with
`pytest-homeassistant-custom-component` and the `home-assistant-frontend`
version required by that Home Assistant release:

```sh
python3 -m venv .venv && . .venv/bin/activate
pip install pytest-homeassistant-custom-component
pip install "$(python -c 'import json,os,homeassistant.components.frontend as f; print(json.load(open(os.path.join(os.path.dirname(f.__file__), "manifest.json")))["requirements"][0])')"
make test-integration
```

The panel test needs Node.js and Playwright (`npm install playwright &&
npx playwright install chromium`).

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
- Anything that comes from the network (server responses, WiFi names, the
  setup page) must be validated before use: see `source/core/validate.c` and
  [SECURITY.md](SECURITY.md). `make test` also runs the fuzzer; for parser
  changes, run it longer: `tests/build/fuzz 5000000`.
- Update the docs and `CHANGELOG.md` if users will notice the change.

By contributing you agree that your contributions are licensed under the MIT
license of the project.
