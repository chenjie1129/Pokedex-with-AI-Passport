# Sparse save migration: device evidence

Verified 2026-09-18 on the connected ESP32-C3 Passport with 8 MiB flash.

## Tested and installed image

- Source commit: `524d7fae8b118d1f69e46c47972575feb4a474d3`.
- Clean build ID: `524d7fae8b11`, `dirty=0`.
- Source SHA-256: `1fe807bc2a3457dd85655cf94bc598cd16f32602d9de8ccb3d3f7ff376b55115`.
- ESP-IDF 5.5.3; ESP32-C3, 8 MiB configuration; all optional smoke modes off.
- Application: 2,839,120 bytes.
- Image SHA-256: `62ed4212373a2bea62cd7347e1a4b0abc3cd2f1b90282762e96aa56f6e18d3c6`.
- Host suite: **41/41 passed**, Debug with address/undefined-behavior sanitizers.
- Firmware build: passed separately. No UI changes in this increment.

## Backup, installation and readback

The target's complete 8 MiB flash was backed up and its partition layout checked
before installation. A fresh NVS read immediately before flashing matched the
backed-up player state. Only the application at `0x10000` was flashed; firmware
performed the versioned NVS migration on first boot.

Full post-install flash readback established:

- Installed application bytes exactly match the clean build image.
- Every byte outside the application partition and NVS is unchanged, including
  bootloader/table, PHY, card identity, recovery and unallocated space.
- All logical NVS keys remain present; only `pokedex/bestiary_v6` changed value.
  Other values, including settings and place data, match their previous hashes.
- The actual C decoder and canonical encoder report identical player state
  before and after migration, including all **20 owned copies**, buddy Pikachu
  (species 25, instance 4), individual attributes and progress counters.
- Save format changed from 12 to 13: **5,524 → 846 bytes**, with seven discovered
  species records and 20 owned records. The runtime model remains schema 12.

## Boot verification

First boot reported `BESTIARY_STORAGE_MIGRATED format=13 model=12 species=16`,
`BESTIARY_READY schema=12 count=20 sequence=21 migrated=1`, the expected buddy,
existing two-place catalog and settings (60% volume, 60% brightness), audio ready,
and `READY display=1 buttons=1 capture_count=20 place_data=1`.

Second boot reported `BESTIARY_STORAGE_READY format=13 owned=20` and
`migrated=0`. Its complete NVS partition was byte-identical to the first-boot
readback: no repeated migration or persistence writes. A final normal boot also
reported `migrated=0` and readiness; the device was left running this image.
The 25-, 20- and 15-second boot captures showed no panic or abort.

## Evidence and limits

Private backup, image, raw readbacks, boot logs, NVS audit and verification JSON
are retained locally under the Git-ignored
`device-backups/pokedex-sparse-20260918/` directory, copied and hash-checked from
the temporary verification workspace. They are not committed because they
contain device-specific data. Retain the pre-upgrade backup for recovery.

This verifies installation and the real format-12-to-13 migration, not physical
button feel, audible playback, capture interaction, field behavior, a long soak,
or power-loss recovery. Stage 2's deferred verification remains unfinished.
Package-only species and catalog-driven runtime progress are not enabled yet.
Older firmware cannot read format 13; use a save-compatible recovery image.
