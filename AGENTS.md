# Repository Guidelines

## Product boundary

- P0 validates whether place-dependent discoveries motivate people to carry
  the device again. Do not add post-MVP features before the product gate.
- The core loop must remain offline and must not require a phone, account,
  GPS, cloud service, or LLM.
- Raw SSIDs, BSSIDs, public BLE addresses, credentials, and precise location
  must never enter persistence or evidence logs.

## Architecture

- Hardware adapters belong in `components/bsp`.
- Hardware-independent game rules belong in `components/city_domain`.
- UI code consumes domain outputs and must not mutate domain persistence
  models directly.
- Keep clocks, random seeds, scan results, and storage outcomes injectable.
- A reward or place confirmation becomes visible only after durable storage
  succeeds.
- Treat scan API failure, successful empty scan, and usable evidence as three
  distinct inputs.

## Embedded constraints

- Target ESP32-C3 with 8 MB flash and no PSRAM.
- Preserve the AI Passport partition and recovery contracts when firmware
  scaffolding is imported from the foundation repository.
- Do not block the LVGL task with scanning, flash writes, or other I/O.
- Batch NVS writes and keep versioned, checksummed persistence formats.

## Validation

Run Host tests before delivery:

```sh
./tools/test-host.sh
```

Report firmware builds and physical device tests separately from Host tests.
Do not describe a Host test pass as hardware validation.
