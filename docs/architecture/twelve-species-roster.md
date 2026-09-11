# Twelve-species roster and encounter status

This device release adds Pikachu, Jigglypuff, Oddish, Meowth, Psyduck, Growlithe,
Geodude, Gastly, and Eevee to the original three starters. `content/species.json`
is the source of stable IDs, names, gameplay stats, descriptions, pool membership,
Wild eligibility and artwork URLs. The generator validates IDs, bounds and stats;
its checked-in tables are verified by the host suite. Generate definitions with
`python3 tools/generate_species.py`; regenerate new artwork with `--images`
(requires Pillow). Existing starter assets retain their original pixels.

Encounter badges use the collection state captured before saving this encounter's
SEEN record: NEW - FIRST ENCOUNTER, SEEN - NOT CAUGHT, CAUGHT - IN BESTIARY.
The snapshot is retained across render/selection changes and save retries. The
encounter itself still appears only after the discovery/reservation write succeeds.
Leaving a NEW encounter makes the next meeting SEEN; catching makes it CAUGHT.
Wild and confirmed-place encounters use the same status logic.

Each confirmed-place pool favors four species at weight 6 and all other species
at weight 1; the selector sums weights rather than assuming a total of 100.
New places prioritize UNKNOWN species, then SEEN-but-uncaught, then the full pool.
Wild includes the six catalog entries marked eligible, with equal weights and the
existing durable cooldown. Stable anonymous place IDs choose pools; the app does
not infer real habitat types from Wi-Fi.

Bestiary records are generic arrays addressed by stable IDs. Four visible list
rows paginate through twelve entries and an explicit Back item. Passport totals
and collection goals use the catalog count. The build-time bound is 32 entries;
this release contains and tests twelve, not a verified 24/32-species device build.

## Save format and migration

V6 uses a 32-byte header, 20-byte records, and CRC32 (276 bytes for 12 species).
Header: magic at 0, schema at 4, record count at 6, settlement high-water at 8,
Wild cooldown flag at 16, catalog version at 20. Records start at 32. Catalog and
firmware versions do not replace save-schema validation. V6 decoding maps IDs,
rejects duplicates/unknown IDs and initializes added catalog entries UNKNOWN.

The BSP reads `bestiary_v6` first. Only a missing key permits migration from
`bestiary_004` (schemas 1–5), then the legacy count. Migration preserves existing
counts, states, stats, last place, sequence and cooldown; it commits and validates
a readback before publishing. Old snapshots are retained for recovery. A corrupt
new key fails visibly instead of falling back. Future settlements remain one blob
and update the in-memory model only after persistence succeeds.

An old firmware rollback needs the corresponding NVS backup; retained legacy data
is an upgrade snapshot and does not track subsequent catches. Back up both app
and NVS before installation. The app and recovery partition layout is unchanged.

## Verification

`./tools/test-host.sh` covers the catalog, every encounter pool, all new species,
pre-encounter status, legacy wire layouts, v6 ordering/duplicates, prior capture
invariants, and the production BSP against a fault-injected NVS interface. It
checks failed staging, commit and readback, recovery after ambiguous success,
legacy retention and no fallback on corruption.

The headless `tests/passport_render` harness executes production render functions
for all twelve details, all list selections, all 36 encounter/status combinations,
and Passport/Home fixtures. Original text is measured before LVGL's ellipsis
mutation, in addition to screen-bound checks. Render checks are distinct from
physical-button and display acceptance. Use the normal ESP-IDF build/package
workflow and verify the running source identity before device handoff.
