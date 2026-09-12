# Install Pokédex on another AI Passport

This installs the **Pokédex firmware**, not the Codex development application.
Use a compatible AI Passport with **ESP32-C3, 8 MB flash**, and the supported
partition layout. The procedure below validates the layout before writing.
It is not a setup guide for blank development boards or other Passport models.

Use one connected Passport at a time. Back up the **target device**: each
Passport keeps its own card identity, recovery image, and save data.

## 1. Get a firmware package

Unzip a versioned package supplied by the project owner. It contains:

- `Pokedex-AI-Passport.bin` — app firmware only.
- `manifest.json` — firmware version, source identity, image size, and SHA-256.
- `SHA256SUMS` — checksums for the package files.
- `check_passport_backup.py` — read-only backup/layout validation.
- `device-installation-guide.md` and `sdkconfig` — instructions and build settings.

Run the following commands from the unzipped package directory. The build
settings file is reference material; it is not flashed.

On macOS:

```sh
shasum -a 256 -c SHA256SUMS
```

On Linux use `sha256sum -c SHA256SUMS`. On Windows use
`Get-FileHash .\Pokedex-AI-Passport.bin -Algorithm SHA256` and compare the result
with `firmware_sha256` in `manifest.json`. Stop if the firmware hash differs.

To build your own package, follow [the README](https://github.com/chenjie1129/city-spirits-passport#build-and-test) in the source
repository. A standalone package does not include the source README.

## 2. Install the flashing tool

Requires Python 3. Create a separate environment:

```sh
python3 -m venv .flash-tools
. .flash-tools/bin/activate
python -m pip install esptool==4.12.0
```

Windows PowerShell equivalent:

```powershell
py -m venv .flash-tools
.\.flash-tools\Scripts\Activate.ps1
python -m pip install esptool==4.12.0
```

## 3. Connect and identify the second Passport

Connect it directly using a **USB data cable**. List serial ports:

```sh
python -m serial.tools.list_ports -v
```

Select the newly connected USB serial device. Typical ports are
`/dev/cu.usbmodem...` on macOS, `/dev/ttyACM0` on Linux, and `COM5` on Windows.
A charging light alone does not establish a USB data connection.

Set the actual port (replace the example):

```sh
PASSPORT_PORT=/dev/cu.usbmodem21201
python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" flash_id
```

The output must identify an **ESP32-C3 with 8 MB flash**. Stop for a different
chip, capacity, or unknown connection.

For the remaining commands, Windows users can set `$PASSPORT_PORT = "COM5"`
and use `$PASSPORT_PORT` in place of `"$PASSPORT_PORT"`. Run each command on one
line, without the shell's `\` line-continuation characters.

## 4. Back up and validate this device

Choose a new backup directory for this Passport. A full read takes a few
minutes. Leave the cable connected and avoid pressing device buttons.

```sh
mkdir passport-backup
python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" --baud 460800 \
  read_flash 0x0 0x800000 passport-backup/full-flash.bin
python check_passport_backup.py passport-backup/full-flash.bin
```

Proceed only when the validator reports a complete backup and matching
partitions. The supported layout is:

| Region | Address | Size | Update policy |
|---|---|---|---|
| NVS saves | `0x9000` | `0x6000` | Preserve |
| PHY data | `0xf000` | `0x1000` | Preserve |
| App | `0x10000` | `0x300000` | Update only this region |
| Card identity | `0x356000` | `0x4000` | Preserve |
| Recovery | `0x700000` | `0x100000` | Preserve |

Keep this backup locally. **Do not share it or use a backup from another
Passport.** Do not run `erase_flash` or flash a bootloader/partition table.
If validation fails, stop and investigate the second device's layout.

## 5. Install the app only

```sh
python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" --baud 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x10000 Pokedex-AI-Passport.bin
```

Wait for `Hash of data verified` and the reset. Do not disconnect during the
write. An optional independent installed-image check is:

```sh
python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" --baud 460800 \
  --after hard_reset verify_flash 0x10000 Pokedex-AI-Passport.bin
```

The new Passport uses its own save data. Existing compatible Pokédex saves
are loaded or migrated; a device without a Pokédex save starts a new
collection. This process does not transfer your first Passport's Pokémon.

## 6. Test the new Passport

1. Confirm the top title reads **Pokédex** and Home opens normally.
2. Test Up, Down, and OK. Open **Sound & screen** and set comfortable sound
   and brightness levels.
3. Try **Look around**, read the first-catch instructions, and catch a Pokémon.
4. Open its entry and **My Pokemon**; use Up/Down to inspect copies.
5. Check that health status is separate from **Heal**. Heal is unavailable
   when the displayed Pokémon has full health.
6. Restart and confirm that the collection and saved settings remain.

New catches save individual stats. Older imported copies may share stats
because older firmware did not record every copy's original values. Those
copies are labeled in the UI.

## Recovery

If the app fails to start, retain the target device's backup and restore only
its original app. First extract the app from its full backup:

```sh
python -c 'from pathlib import Path; b=Path("passport-backup/full-flash.bin").read_bytes(); assert len(b)==0x800000; Path("passport-backup/factory.bin").write_bytes(b[0x10000:0x310000])'
python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" --baud 460800 \
  --after hard_reset write_flash 0x10000 passport-backup/factory.bin
```

If a save migration occurred, the old app may also need its original NVS.
Restore that only from **this same device's pre-installation backup**; doing
so discards any progress made after the backup. Ask the project owner to
perform that recovery if unsure. Never restore another device's identity or
write the whole backup as a shortcut.

If no USB port appears, try another data cable and a direct USB connection.
If a port is busy, close serial monitors. If flashing cannot connect, use the
Passport's documented BOOT/reset procedure; do not erase it to solve a
connection problem.
