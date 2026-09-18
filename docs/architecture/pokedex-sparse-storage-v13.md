# Stage 3 increment 5: sparse save storage

**Current integration:** see [Stage 3 runtime](pokedex-stage3-runtime.md). The boundaries and measurements below describe the earlier increment.

Implemented 2026-09-18. Storage format **13** omits undiscovered species and unused
owned-copy slots. The gameplay model remains version 12 with its current compiled
roster and 160-owned-copy bound. This is the durable-storage migration increment;
package-only captures and catalog-dependent progress models are still unfinished.

## Wire contract

Retain the existing 48-byte header, 22-byte species record and 32-byte owned-copy
record encodings, with these explicit format-13 rules:

- Header version at offset 4 is 13; offset 6 counts only non-UNKNOWN records and
  may be zero. These records follow the header in strictly ascending stable-ID
  order. Missing compiled species initialize to UNKNOWN without fabricated copies.
- Offset 26 is the actual owned count. Exactly that many owned records follow
  the species records, retaining their original order and every field.
- Length is `48 + discovered_count * 22 + owned_count * 32 + 4`; IEEE CRC-32 is
  stored in the final four bytes. No unused padding or trailing bytes are accepted.
- Preserve cooldown, settlement/visit sequences, buddy species/instance,
  next-instance counter, stats, HP, evolution, personality, friendship and memories.
- Retain the existing reserved-byte, stat-range, unique-instance and consistency
  checks. Reject duplicate/unsorted/UNKNOWN encoded records, unsupported IDs,
  future formats, truncation, trailing bytes and invalid CRC. Never discard an
  unsupported saved ID or silently replace a corrupt save with legacy progress.

`city_bestiary_encode_sparse` writes only after validating the model and caller
capacity; failures leave both output and written length unchanged. It allocates
no heap memory. `city_bestiary_decode` accepts legacy formats and format 13 into
the existing model, using its existing heap-backed transactional decode.
`city_bestiary_encode` remains the legacy format-12 canonical encoder for existing
fixtures and exact before/after audits; production NVS writes use the sparse API.

This does not make the in-memory bestiary independent of the compiled roster.
The next gameplay-model increment must supply bounded sparse records and catalog
lookup for package-only IDs before those species can be captured. Adding unknown
compiled entries no longer requires a format-13 save rewrite merely to reserve
empty records. Current unsupported package IDs fail closed on load.

## Durable migration

The `pokedex/bestiary_v6` NVS key is retained. Its value is versioned, rather than
adding another key that could silently revive an outdated collection. On load,
older compatible formats are decoded, written as format 13, committed, read back,
and compared via canonical encoding before the model is published. Failed set,
commit or readback leaves the caller's model unchanged. A commit whose readback
fails can already be durable; the next boot loads that valid format-13 blob
without repeating migration. New writes store the exact compact length.

An invalid primary value never falls back to a stale legacy key. Format-13
primary values do not migrate repeatedly because their record count differs
from the full catalog size. Ordinary saves retain the existing NVS commit policy.

The old firmware cannot read format 13. **Do not downgrade by flashing an older
app after migration.** Preserve the pre-upgrade backup for deliberate recovery;
this increment provides no automatic downgrade or save-destructive recovery.
Only the application partition is flashed; the running firmware performs the
versioned save migration. Partition layout, card identity and recovery stay intact.

## Verification

- 41/41 host tests pass, including existing gameplay/navigation/persistence tests.
- Format-12 → format-13 → canonical state roundtrips preserve all fields, including
  different copies of one species, buddy, damaged HP, friendship/memory, release
  history, cooldown and counters. Empty and full-capacity saves pass.
- Every byte corruption and every truncation of fixture saves is rejected;
  valid-CRC malformed records, IDs, order and reserved fields are rejected.
- Production BSP adapter tests cover failed set/commit/readback, retries,
  legacy migration, corrupt primary data, smaller old catalogs and no repeated
  writes after migration.
- Read-only `build/host/bestiary_migration_audit BEFORE [AFTER]` compares decoded
  saves using canonical bytes. Raw device saves/backups stay outside the repository.
- ESP-IDF 5.5.3 / ESP32-C3 clean build and app-only installation pass. Full flash
  readback, exact player-state preservation and repeat-boot checks are recorded
  in [device evidence](../verification/pokedex-sparse-device-2026-09-18.md).

Stage 2 physical tests remain deferred; this migration's device evidence does
not substitute for the pending interactive soak, field and physical-failure tests.
