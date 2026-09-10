# W1 Environment Foundation

## Scope

This slice implements hardware-independent place classification and Wild Mode
reward protection. Device scanning, NVS adapters, UI, spawning, and capture
remain outside this change.

## Data flow

```text
Wi-Fi BSP
  -> city_environment_sample_t (raw identifiers, RAM only)
  -> city_place_fingerprint_build (keyed tokenization)
  -> city_place_fingerprint_t (tokens only)
  -> city_location_mode_step
  -> known / gray / candidate / Wild decision
```

The 128-bit device key is an injected secret. The domain component neither
generates nor persists it. The firmware adapter must provision it separately
from place profiles.

## Place rules

- A usable fingerprint contains at least four and at most twelve AP tokens.
- Similarity uses overlap coefficient:
  `intersection / min(left_count, right_count)`.
- Greater than 60% means known place.
- 30% through 60%, inclusive, means gray zone.
- Less than 30% means new-place evidence.
- A new place requires two mutually consistent scans at least 20 seconds
  apart.
- A confirmed known place is locked for five minutes.
- Scan failure preserves the previous state.
- A successful empty or insufficient scan enters Wild Mode after any active
  known-place lock expires.

## Persistence contract

`city_place_catalog_encode` writes a stable little-endian format containing:

- format magic and schema version;
- unique local place IDs;
- confidence and last-confirmed time;
- unique keyed AP tokens;
- CRC-32 integrity protection.

It contains no raw SSID or BSSID fields. Decode is transactional: the caller's
catalog is unchanged if validation fails.

The location engine uses two-phase confirmation:

1. `city_location_mode_step` returns `NEW_PLACE_READY`.
2. The application creates and durably writes the profile.
3. Only after successful persistence may it call
   `city_location_mode_commit_place`.

## Wild reward contract

`city_wild_reward_guard` allows one settled Wild reward per 30 minutes. Its
snapshot records whether a cooldown is active. On restart, an active or
unknown-version snapshot starts a fresh 30-minute block. This is intentionally
conservative because the MVP has no trusted wall clock: it may delay a reward,
but it cannot grant a duplicate reward after reboot.

## Host verification

```sh
./tools/test-host.sh
```

The test build uses C11, warnings as errors, AddressSanitizer, and
UndefinedBehaviorSanitizer by default.
