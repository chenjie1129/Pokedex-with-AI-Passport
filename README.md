# Pokédex for AI Passport

An offline Pokémon collecting prototype for the ESP32-C3 AI Passport: look
around, catch Pokémon, collect place stamps, and choose a buddy. The device UI
is called **Pokédex**; this repository retains the City Spirits Passport name.

The current prototype uses Pokémon names, images, and cries for internal
playtesting. The original City Spirits product plan is preserved in
[the product proposal](docs/product-proposal.md); future original content and
broader product goals remain in [ROADMAP.md](ROADMAP.md).

## Current experience

- **Look around:** discover Pokémon using coarse, local Wi-Fi place recognition.
  No phone, account, GPS, cloud service, or LLM is required.
- **Capture:** press OK when NOW appears. A first-catch guide waits until the
  player is ready; the timed round gives three attempts in 15 seconds.
- **Pokédex:** browse 15 species/forms, types, short facts, discovery state,
  owned counts, and saved stats. Species cries play offline.
- **My Pokemon:** compare individual copies using Up/Down. Each page shows
  that copy's Health, Attack, Defense, and capture origin.
- **Buddy choices:** health and “Health is full” / “Needs healing” are status
  text. **Heal** is a separate action, unavailable at full health. It heals the
  strongest representative shown by the species summary.
- **My stamps:** collect stamps for confirmed places and see the next goal.
- **Evolution:** starter buddies can grow into Ivysaur, Charmeleon, or
  Wartortle after reaching 30 friendship and visiting three saved places
  together through captures.
- **Let go:** select a specific copy, then confirm. **Keep it** is the default.
- **Sound & screen:** adjust sound volume, quiet mode, and screen light.
  Choose **Save and go back** to persist changes or **Undo and go back** to
  discard them.
- **Wild Mode:** sparse surroundings offer a separate encounter pool with
  one opportunity every 30 minutes. Leaving uses that opportunity; restarting
  can restart the wait. It does not add a place stamp.

### Controls

| Screen | Up / Down | OK | Hold |
|---|---|---|---|
| Home | Choose a destination | Open | Hold Up to turn the screen off |
| Pokédex list | Choose a species | Open | Hold OK to go Home |
| Owned species detail | Up: make buddy; Down: back | Open choices | — |
| Buddy choices | Choose an available action | Select | — |
| My Pokemon | Previous / next copy | Back to choices | — |
| Catching | — | Throw when NOW appears | Hold OK to stop |
| Let go picker / confirmation | Choose copy or decision | Select / confirm | Hold OK to go back |
| Sound & screen | Choose a row or change a level | Edit / finish / select | Hold OK to undo changes |

A wake-up button press only wakes the screen. It does not also select a menu
item. Screen-specific instructions remain visible at the bottom.

### Why some older copies have identical stats

New encounters generate individual stats from species base values plus a
per-encounter variation. Each new catch saves its own stats and identity.

Older save versions stored only the latest and best stats for each species.
When those saves were upgraded, older copies inherited the known values;
the original individual values cannot be recovered. Such copies now say
**From your old save / Stats may be shared**. Their history is preserved
without inventing new attributes.

The species detail shows the **strongest** representative, while **My Pokemon**
shows each individual. When a new strongest copy is caught, the summary takes
that copy's health as well as its stats. See
[health, individual copies, and persistence](docs/architecture/persistent-hp-and-release.md).

## Install on another Passport

Download the [tested firmware package](https://github.com/chenjie1129/city-spirits-passport/releases/download/v0.1.0-pokedex-preview.1/pokedex-tested-20260912.zip) or the [standalone .bin](https://github.com/chenjie1129/city-spirits-passport/releases/download/v0.1.0-pokedex-preview.1/Pokedex-AI-Passport.bin) from the [playtest release](https://github.com/chenjie1129/city-spirits-passport/releases/tag/v0.1.0-pokedex-preview.1). The package includes instructions and checksums, and contains no player saves or device identity data.

Use a **compatible AI Passport with ESP32-C3, 8 MB flash, and the partition
layout in [partitions.csv](partitions.csv)**. An arbitrary ESP32 board or a
Passport with a different layout needs separate setup.

Follow the [step-by-step installation guide](docs/device-installation-guide.md).
It covers USB detection, firmware checksum verification, a backup of the
**second device**, partition validation, app-only installation, and rollback.
Only `Pokedex-AI-Passport.bin` is installed at **0x10000**. Never copy another
Passport's NVS, card identity, full-flash backup, or recovery image onto it.

Prebuilt firmware is distributed as a separate versioned package with
`manifest.json` and `SHA256SUMS`; firmware binaries and private device backups
are not tracked in this repository. A package can be created from source as
shown below. Codex itself is not installed on the Passport—the device runs the
Pokédex firmware.

## Build and test

### Host tests

Requires a C compiler, CMake, and Python 3:

```sh
./tools/test-host.sh
```

The suite currently contains 26 tests, covering encounters, persistence,
individual copies, healing, release, evolution, settings, navigation, audio,
and generated assets. Sanitizers are enabled by default where supported.

### Firmware

The tested toolchain is ESP-IDF **v5.5.3**. With ESP-IDF installed:

```sh
. "$IDF_PATH/export.sh"
idf.py -B build/firmware set-target esp32c3
idf.py -B build/firmware \
  -D CITY_CAPTURE_RENDER_SMOKE=OFF \
  -D CITY_AUDIO_RENDER_SMOKE=OFF \
  -D CITY_SETTINGS_SMOKE=OFF build
```

The normal app must fit the 3 MiB `factory` partition. A size warning for the
separate 1 MiB recovery partition does not make the normal app a recovery
image. Install using the app-only guide, not a whole-device `idf.py flash`.

The human-facing firmware version is maintained in the root [`VERSION`](VERSION)
file using SemVer, such as `0.1.0-dev`. Git commit, source fingerprint, and
dirty state are separate build metadata; they remain available in the build
manifest and boot log without making the device version hard to read.

Package a matching clean build outside the repository:

```sh
python3 tools/package_firmware.py \
  --build-dir build/firmware \
  --output-dir ../pokedex-package
```

For a deliberately labeled local device test, add `--allow-dirty`. The
packager checks source identity, embedded firmware version, and image size.
It refuses stale or mismatched build artifacts.

### UI renders and generated content

After ESP-IDF has populated `managed_components/lvgl__lvgl`:

```sh
cmake -S tests/passport_render -B build/ui-render
cmake --build build/ui-render
mkdir -p build/ui-screens
(cd build/ui-screens && ../ui-render/passport_render)
```

The render harness checks glyph availability, clipping, hidden ellipses,
parent bounds, and text overlap at 240×320. An optional local decoded game
blob can be passed as the renderer's argument to inspect real saved copies;
never commit that blob or device backups.

```sh
python3 tools/generate_species.py --check
# Only when regenerating the committed UI fonts; requires lv_font_conv 1.5.3:
python3 tools/generate_ui_fonts.py /path/to/lv_font_conv
```

The UI fonts include the accented **é** in Pokédex at both display sizes.
The browser demo in `demo/` is an earlier capture simulator, not a mirror of
the current device UI.

## Verification status

The September 12, 2026 device-test build passed 26 host tests and 381 rendered
screen states, including 25 saved individual Pokémon. The strongest-copy
health regression test fails on the old implementation and passes with the fix.

The app was installed on an ESP32-C3 Passport; its digest and protected flash
regions were verified. Startup reached Home with the same collection, buddy,
places, and settings. The owner subsequently accepted the experience.
These checks are distinct from a broader child-comprehension or field trial.

## Architecture and further reading

Hardware adapters live in `components/bsp`; game rules and save formats live
in `components/city_domain`; `main/` renders the LVGL UI and coordinates work.
Writes persist before the UI grants rewards. Scanning, NVS, and audio avoid
blocking the LVGL task. The project preserves the Passport recovery contract.

- [Repository rules](AGENTS.md)
- [Place recognition](docs/architecture/t08-real-place-integration.md)
- [Wild Mode and pocket controls](docs/architecture/wild-mode-and-pocket-controls.md)
- [Individual copies, health, and release](docs/architecture/persistent-hp-and-release.md)
- [Starter evolution](docs/architecture/starter-evolution.md)
- [Device settings](docs/architecture/settings.md)
- [Pokémon cries](docs/architecture/pokemon-cries.md)
- [Catalog and sources](content/species.json)
- [Firmware identity](docs/architecture/encounter-eligibility-and-build-identity.md)
