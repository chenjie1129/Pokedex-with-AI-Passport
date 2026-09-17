# Controlled NVS fault tests — 2026-09-15

Result: **8/8 on-device diagnostic cases passed; normal game restored**.
This is a local diagnostic result, not full Stage 2 release certification.

## Verified

- ESP-IDF 5.5.3 / ESP32-C3 diagnostic builds; host suite **33/33 passes**.
- Before-set failure: caller unchanged; complete save recovered.
- After-set / before-commit failure: caller unchanged; complete save recovered.
- Injected commit error: caller unchanged; complete save recovered.
- Post-commit read-back error: caller unchanged; complete save recovered.
- Corrupt current save rejected without falling back to legacy count 99.
- 64 changing saves exercised real NVS writes and recycling in a 24 KiB scratch store.
- Actual `ESP_ERR_NVS_NOT_ENOUGH_SPACE`: failed capture did not change caller;
  previous save remained readable; filler cleanup and reopen allowed a new save.
- Actual software restart after commit, before caller publication: capture
  survived, retry was a duplicate, and the count remained exactly once.
- Successful diagnostic minimum heap: **266,764 bytes**.

## Preservation and restore

A new full backup was captured after the owner's hands-on play test. It contained
20 owned Pokémon, buddy instance 4, schema 12 / catalog revision 6.

Only the app image and guarded app-tail scratch area were used. The diagnostic
never initialized the player NVS partition. The scratch was erased on success.
Normal firmware from the prior tested candidate package was then reinstalled.

**The full 8,388,608-byte flash readback equals the fresh pre-test backup byte for
byte.** This verifies restoration of app, all player data/settings/place state,
blank scratch, boot/layout, PHY, card identity and recovery. Normal boot reported
20 captures, selected buddy retained, `migrated=0`, display/buttons/place data ready.

## Diagnostic correction during validation

The first run aborted at the initial guard write because ESP-IDF protects the
entire running app partition. No fault cases had started. The normal game was
restored while the standalone diagnostic configuration was corrected.

The successful test-only build permits app-tail writes with strict runtime layout,
image-end and guard ownership checks. The normal game's flash protection remains
`CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS=y`. The observer now rejects explicit
abort messages as well as panics, incomplete output and test failures.

## Artifacts

- Source and procedure: `tests/device_nvs_fault/README.md`.
- Observer: `tools/run_nvs_fault_test.py` (does not flash or erase).
- Local reusable package: `../device-packages/pokedex-nvs-fault-20260915/`.
  Includes diagnostic app, exact normal restoration app, manifests, guide,
  observer and verified SHA256SUMS. No device backups or player data included.
- Private evidence: `/private/tmp/pokedex-fault-20260915/`; passing log is
  `run-2.log`, preservation result is `preservation.json`. Do not publish raw
  full-flash files or extracted player saves.

## Limits

Injected NVS API errors are controlled software failures, not physical chip
faults. A software restart is not a battery/power cut. These tests exercise the
production adapter and domain publication boundary, not the game's on-screen
storage-error messages. Full-capacity place/UI integration, physical power cuts,
two-location scanning and long soak remain separate acceptance work.
