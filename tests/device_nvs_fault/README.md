# Controlled NVS failure diagnostic

This standalone ESP32-C3 app runs eight automatic checks and reports results over
USB serial. It does not run the game UI. The screen may be blank or retain its
last image; the serial result is authoritative.

The production save adapter is compiled here with four NVS call substitutions.
The normal game build contains none of these test wrappers. Calls are routed only
to a disposable `fault_scratch` partition descriptor, not player NVS.

## Storage boundary

- Supported device: 8 MiB AI Passport; existing factory app at `0x10000`, size
  `0x300000`. No partition-table or bootloader changes.
- Reserved app tail: guard sector `0x309000..0x309fff`, NVS scratch
  `0x30a000..0x30ffff` (24 KiB, matching the player NVS capacity).
- Runtime verifies the installed image ends before the guard sector. The area
  must be erased, or carry this diagnostic's guard and NVS cookie. An unknown
  occupied area causes FAIL; it is not erased to make the test work.
- No `nvs_flash_init()` for the player partition, no player namespaces, and no
  radio, settings or game writes. The scratch region is erased after success.
- On failure the app stops and retains scratch evidence. Restore the normal
  app and investigate before rerunning; never erase player NVS to fix a test.

The standalone diagnostic enables `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED`
because ESP-IDF otherwise protects the whole running app partition, including
its blank tail. The runtime layout, image-end and ownership checks above are
mandatory before scratch writes. This configuration applies only to this test
project; never copy it into normal game settings.

## Cases

1. Return an error before `nvs_set_blob`.
2. Write the blob, then return an error before commit.
3. Inject a commit-call error.
4. Commit succeeds, then migration read-back returns an error.
5. Reject a corrupt current blob despite a valid legacy count.
6. Perform 64 changing saves on actual NVS (page recycling).
7. Fill actual scratch NVS until `ESP_ERR_NVS_NOT_ENOUGH_SPACE`, verify an
   unsuccessful capture leaves the caller unchanged and prior save readable,
   remove disposable filler, reopen NVS and save successfully.
8. Software restart after capture commit, before caller publication; resume
   across boot and verify the capture exists exactly once and retry is duplicate.

Cases 1–4 require unchanged caller state, then complete recovery after NVS
reinitialization. Real NVS can make a write durable before the application calls
commit; an error is not proof that nothing reached flash. The recovery assertion
therefore requires a complete valid state, with no duplicate capture.

These tests verify the storage/domain publication boundary, not an on-screen
storage-error message. Cases 1–4 inject API errors; they do not manufacture a
physical flash-chip fault. Software restart is not battery/power interruption.

## Build

With ESP-IDF 5.5.3 activated, from the repository root:

```sh
./tools/test-host.sh
cd tests/device_nvs_fault
idf.py -B ../../build/nvs-fault build
```

## Run

1. Identify the connected Passport. Make a fresh full 8 MiB backup and validate
   it with `tools/check_passport_backup.py`; retain the tested normal app for
   restoration. Check the entire `0x309000..0x30ffff` region is blank before
   first installation. Keep backups and raw logs private.
2. Flash **only** `build/nvs-fault/pokedex_nvs_fault_test.bin` at `0x10000`.
   Do not use generic `idf.py flash`, which also writes bootloader/partition data.
   Use esptool `--after no_reset` so the observer can start before the test boots.
3. From the repository root, with pyserial and esptool installed:

```sh
python tools/run_nvs_fault_test.py --port /dev/cu.usbmodemYOUR_DEVICE \
  --log /absolute/private/path/fault-run.log --reset --timeout 180
```

The observer resets once, follows the deliberate second boot, and requires all
8 case markers plus `FAULT_SUITE_PASS ... scratch=erased`. Missing markers,
timeout or a panic means incomplete/failed, not pass. Use a fresh log path.

4. Read back flash. Require player NVS (`0x9000..0xefff`), boot/layout, PHY,
   card identity, recovery and all non-app bytes to match the fresh backup.
   Confirm the guard and scratch are erased.
5. Restore **only** the previously tested normal app at `0x10000`. Verify the
   app hash and normal boot, then verify collection, buddy and settings.

A successful run leaves the diagnostic idle. Resetting starts another complete
run. Reinstalling the normal app restores ordinary gameplay.

## Still separate

A real power-cut campaign needs a spare/test device and controlled interruption
of its actual supply; unplugging USB while its battery powers it is insufficient.
This diagnostic does not provide a timed physical-power trigger. Long soak,
two-location scans, and UI error-message acceptance remain separate checks.
