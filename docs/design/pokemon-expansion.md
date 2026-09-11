# Pokémon expansion design — 12 now, capacity for 24

Status: proposal only, 2026-09-11. No firmware or device changes in this design task.
Code baseline: b8f28dc. Size baseline: the installed Passport build
5eab96dfd7c2-76ef906d73-dirty, whose feature source was committed as b8f28dc.

## Recommendation

Add nine species for 12 total. Refactor the catalog, save model and list UI once
so the same architecture can accommodate 24. Keep all gameplay offline and keep
the existing flash partition layout. A server database is unnecessary for this
expansion. Treat 24 as the planned capacity, not a measured 24-species build.

## Measured storage and estimated capacity

The board has 8 MiB flash, but this application's factory partition is only
3 MiB (3,145,728 bytes). Other partitions and unassigned address ranges cannot be
assumed available to the app; preserve the bootloader, card identity and recovery
contract. The current firmware is 1,423,904 bytes (1.358 MiB).

Counting the generated image arrays gives exactly 56,472 bytes per species:
110 × 110 × 3 = 36,300 bytes, plus 82 × 82 × 3 = 20,172 bytes.
RGB565A8 uses two color bytes plus an alpha byte per pixel. The three species use
169,416 bytes. Large C source file sizes are text representation, not flash cost.

First-order estimate: 1,423,904 + (N − 3) × 56,472 bytes. This assumes the current
art sizes/format and excludes additional code, metadata, descriptors, alignment
and save-model changes. It is a sizing model, not a firmware validation result.

| Total species | Added | Estimated app | Remaining app space | Decision |
|---|---:|---:|---:|---|
| 12 | 9 | 1.843 MiB | 1,213,576 B | Recommended next release |
| 24 | 21 | 2.489 MiB | 535,912 B | Planned capacity; build/heap test required |
| 28 | 25 | 2.704 MiB | 310,024 B | Little room after a 256 KiB reserve |
| 30 | 27 | 2.812 MiB | 197,080 B | Too tight for the proposed reserve |
| 33 | 30 | 2.974 MiB | 27,664 B | Arithmetic ceiling only; do not target |
| 34 | 31 | 3.027 MiB | Negative | Does not fit |
| 151 | 148 | 9.329 MiB | Negative | Current artwork strategy cannot fit |

Require at least 256 KiB free in the app partition after the actual final build.
For 24 species the model leaves roughly another 267 KiB for implementation growth
beyond that reserve. More animation frames multiply artwork cost.

A larger offline catalog is possible with a different asset strategy. For example,
a single 96×96 I8 image and 1,024-byte palette per species costs 10,240 bytes;
151 such images plus the current non-image baseline is about 2.671 MiB. This is
only a feasibility calculation: image quality, secondary-size scaling, decoder
buffers, additional code and the larger save model remain untested. LVGL supports
indexed formats, but palette images can require decoding memory. Do not promise
151 species from this calculation. Benchmark I8/RLE on representative artwork
before selecting compression or changing the current two-image design.

## RAM and local saves

The current artwork arrays are const data backed by flash. Keep them there and
show only one large/one small creature as required by the current screen. Adding
catalog entries must not preload all images or construct a UI row for every entry.
The current display DMA buffer is 240 × 20 × 2 = 9,600 bytes. No PSRAM is assumed.
Boot heap figures are not peak scan/render headroom; measure minimum heap and the
largest internal/DMA blocks during real scanning and captures before release.

The 24 KiB NVS partition stores progress and the place catalog, not pictures.
Save records currently occupy 20 wire bytes per species. A proposed v6 header of
32 bytes, 20-byte records and a 4-byte CRC would need 276 bytes for 12 species or
516 bytes for 24. NVS metadata, old/new copies and garbage collection need extra
space; these are payload figures, not total NVS allocation. Keep up to 32 records
in the first implementation; the wire record count/IDs are extensible. A larger
catalog must raise the bounded limit and remeasure stack/heap and NVS behavior.

Continue storing aggregate counts and latest/best stats per species. This design
does not store every individual caught creature or an unlimited capture journal.
Those are separate product/storage requirements.

## Required architecture changes

1. **One data-driven species catalog.** Add a checked-in catalog with stable
   species ID, name, display order, element, short description, gameplay stats,
   place affinity, Wild eligibility, asset source and content version. A build
   generator produces const definitions, image references and lookup tables.
   No runtime JSON parsing or network lookup. Validate unique IDs, asset presence,
   positive weights and byte-sized gameplay stats (base <= 240 before the existing
   +0…15 variation). Keep current three species' stat definitions compatible with
   existing saves. These remain game stats, not a promise of official battle rules.

2. **Generic bestiary records.** Replace the named bulbasaur/charmander/squirtle
   struct fields and switches with bounded records addressed by stable species ID.
   Species count, selection, artwork and Passport totals come from the same catalog.
   Adding a species should require one catalog entry plus assets, not edits to
   several independent species lists. Separate catalog version from save schema
   and firmware/source identity.

3. **Safe v6 migration.** Preserve counts, discovery state, latest/best stats,
   last place ID, settlement high-water sequence and the Wild cooldown flag.
   Decode v1–v5, map old records by ID, initialize new ones UNKNOWN, write one new
   checksummed blob, then read back/validate before showing the migrated model.
   Use a new key for v6 and retain the old blob as an upgrade snapshot. Valid v6
   takes precedence; missing v6 permits migration; corrupt v6 must report an error,
   not silently revert to stale progress. Persist the entire settlement in one
   blob so reward and record cannot diverge. Reject catalog removal/incompatible
   IDs explicitly in this release instead of dropping data. The retained old save
   is not continuously updated; firmware rollback requires its matching backup.

4. **Four visible bestiary rows.** At most four rows are created, selected item
   scrolls the window, page indicator shows position. UP/DOWN chooses, OK opens
   detail, hold OK returns Home. Keep an explicit Back item reachable in the list.
   Detail returns to its selected row. UNKNOWN/SEEN/CAUGHT stays familiar; no extra
   thumbnail assets needed. Passport denominator becomes 12, then 24 automatically.
   Already-collected records and stamps remain intact when the denominator grows.

5. **Generalized encounter weights.** The existing 60/25/15 code hard-codes a
   total of 100; increasing only CITY_SPECIES_COUNT would produce wrong selection
   probabilities. Sum actual eligible weights on every branch, use wide totals,
   reject empty pools and test that every intended species is reachable.

## Proposed next roster

Keep Bulbasaur, Charmander and Squirtle. Add:

| ID | Species |
|---:|---|
| 25 | Pikachu |
| 39 | Jigglypuff |
| 43 | Oddish |
| 52 | Meowth |
| 54 | Psyduck |
| 58 | Growlithe |
| 74 | Geodude |
| 92 | Gastly |
| 133 | Eevee |

Use three neutral content pools A/B/C of four species each, assigned
deterministically from anonymous place ID and a fixed pool seed. These are
fantasy content preferences. Display no claim that Wi-Fi identifies water,
parks or streets.

Every confirmed-place pool gives all 12 species nonzero probability: four favored
species have weight 6 and the other eight weight 1 (sum 32; favored group 75%).
Group A: Bulbasaur, Oddish, Geodude, Gastly.
Group B: Charmander, Pikachu, Growlithe, Meowth.
Group C: Squirtle, Psyduck, Jigglypuff, Eevee.
On a newly confirmed place, prioritize UNKNOWN species; if none, prefer SEEN but
uncaught species; if none, use the full pool. This guarantees an eligible encounter,
not a successful catch. Existing timing and durable eligibility rules remain.

Wild pool: Bulbasaur, Squirtle, Oddish, Psyduck, Jigglypuff and Meowth, initially
equal weights and the existing saved 30-minute opportunity cooldown. The other
six remain place-only, giving confirmed places distinct collection value.
No evolution, trading, battles or time-of-day availability in this expansion.

Keep the existing controlled-prototype asset sourcing convention in
`demo/assets/SOURCES.md`: source and track each artwork; a source URL alone is
not a new redistribution grant. Public product assets remain a separate decision.

## Do we need a server database?

| Data or capability | Recommended location now | Server needed? |
|---|---|---|
| Species definitions and weights | Versioned catalog in Git, compiled into app | No |
| Images | Device flash, built with firmware | No |
| Catches, stats, Wild cooldown | Local NVS | No |
| Anonymous confirmed places | Existing local NVS catalog | No |
| Release distribution | Existing downloadable firmware package | No database |
| Optional future content downloads | Static versioned package hosting plus local cache | Usually no database |
| Account backup or cross-device sync | Optional authenticated backend with conflict rules | Yes, only if this feature is chosen |
| Authoritative trades, competitive rewards | Backend ledger and abuse controls | Separate future scope |

A server does not increase flash or RAM on the ESP32. Downloaded images still
need storage or buffers, and fetching at encounter time would break offline play.
Even future content updates should be installed as validated versioned packages
and remain playable without a connection. Start with the existing app package
workflow; do not repartition flash or build SQL/account infrastructure for 12–24
species. None of the current gameplay data requires SQL.

## Delivery and release gates

A. Generic catalog, v6 migration and paginated UI, still with the original three
species. Host/device migration tests must preserve the user's actual save.
B. Add nine species, generated artwork and the defined pools; verify each species
is reachable and viewable. Run 12-entry and synthetic 24-entry catalog fixtures.
C. Build the real 12-species firmware and enforce the 256 KiB app reserve. Use a
24-species asset-capacity build before claiming 24 supported. Keep recovery intact.
D. Exercise all list boundaries and longest names with actual LVGL renders;
check 0/1/12/24 counts, Back, wake handling and Passport goals. Simulate seeded
encounters to test eligibility, reachability and weighted distribution.
E. Inject failed writes and restart after each migration/settlement stage. Fill
NVS with realistic existing place data and repeat saves through garbage collection;
full storage must show an error without fabricated collection progress.
F. On device: repeated page/detail/capture cycles, active scan peak memory,
restart retention and the planned two-hour outing. Record min free heap, largest
internal/DMA block, capture responsiveness and battery readings; any allocation
failure or save regression blocks expansion. Runtime budget thresholds should be
set from the baseline measurement, not guessed from boot heap totals.

## Evidence

Local sources inspected: partitions.csv; the built Passport .bin; both sprite
array sources and conversion tools; bestiary_service.h/.c; encounter_selector.c;
main/main.c; passport_progress.c; BSP NVS and display adapters. The capacity
calculation counts actual array bytes; no larger catalog was built in this task.

- [ESP-IDF v5.5.3 NVS documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/storage/nvs_flash.html): small-value storage, free-entry requirements, power-loss behavior and heap costs.
- [LVGL 9.5 image formats](https://lvgl.io/docs/open/9.5/main-modules/images/color_formats.html): RGB565A8 and indexed-format support.
