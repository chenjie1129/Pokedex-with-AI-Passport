# Stage 2 candidate verification — 2026-09-15

Status: **candidate migration verified; Stage 2 release gate pending**.
Branch: `feat/pokemon-pokedex`, based on `22a3961`, local uncommitted changes.
This is a labeled device-test build, not a clean-source release certification.

## Delivered

- Catalog revision 6 adds original CC0 Mossbit (stable local ID 60000).
- Original bitmap and synthesized cry are reproducible with
  `tools/generate_mossbit.py`; no new third-party asset is included.
- Mossbit remains excluded from both wild and place encounters. Existing
  place weights remain unchanged. It is not a National Pokédex species.
- Immutable compiled `city_catalog_provider_t` separates definitions from
  save/game rules. Runtime remains bounded at 16 entries.
- Same-schema expansion tests cover failed set, failed commit, committed data
  with failed read-back, unchanged caller state, and retry without duplicate writes.
- Compatibility matrix: `docs/architecture/pokedex-save-compatibility.md`.
- Fixed long version text clipping in Settings discovered by the render fixture.

## Evidence

| Layer | Result |
|---|---|
| Host suite | 32/32 pass, including adapter fault injection and original-ID validation |
| Production-derived LVGL renders | 601 fixtures pass text fit, glyph, bounds and overlap checks; representative images visually inspected |
| ESP-IDF | 5.5.3, ESP32-C3, build succeeds |
| Baseline app | 2,772,096 bytes; 373,632 bytes factory headroom |
| Candidate app | 2,838,480 bytes; 307,248 bytes factory headroom (above 256 KiB gate) |
| Backup | Fresh full 8 MiB backup; partition validator passed before installation |
| Actual NVS migration | schema 12 / 15 entries to schema 12 / 16 entries |
| Save preservation | All 15 records and all owned slots byte-identical; nine individuals retained; buddy instance 4 retained |
| New record | ID 60000 UNKNOWN; no captures, no owned instance; unset place sentinel retained |
| Other NVS | All 24 non-bestiary live entries unchanged, including settings/place data |
| Protected flash | Boot/layout, PHY and all regions above app unchanged, including identity/recovery |
| Binary verification | Application readback matches installed candidate bytes |
| Reboot | `migrated=0` on subsequent boot, 16 entries, nine captures, display/buttons/place data ready |
| Heap | 184,980 bytes minimum at catalog-load checkpoint; not a long-run minimum |

Private raw evidence and backups remain outside Git at
`/private/tmp/pokedex-stage2-20260915/`. Save bytes and hardware identifiers must
not be published. The final sanitized device-test package is under
`../device-packages/pokedex-stage2-candidate-20260915/`; its manifest and
SHA256SUMS identify the exact packaged build. It contains no saves or full-flash
backup. `dirty=true` is intentional and prevents treating it as release evidence.

## Remaining gate work

- Controlled real-NVS-full, set/commit/read-back faults and power interruption
  on a spare/test device (Round C). Host mocks are not hardware fault evidence.
- Full Round A scan/button/audio acceptance, two distinct physical locations,
  30-minute interaction baseline, and final two-hour soak.
- Content decision for a production 16th entry; Mossbit is a migration candidate.
- Clean-source build, release evidence review and owner gate decision.

Do not mark Stage 2 complete or advance to Stage 3 on this evidence alone.
Do not downgrade a migrated device to a 15-entry image without an explicit,
device-specific restore plan.

## Follow-up: controlled faults

The subsequent `pokedex-nvs-fault-2026-09-15.md` report records 8/8 on-device
diagnostic cases, including real NVS exhaustion and software restart recovery.
Physical power interruption and long soak remain unverified. The owner reported
the candidate looked good in hands-on testing; this is not a claim that every
remaining scripted acceptance case was performed.
