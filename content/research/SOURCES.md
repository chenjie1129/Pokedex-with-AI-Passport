# Pokédex research sources

This directory contains a source census for architecture and capacity research.
It is not the production firmware catalog and must not be copied into
`content/species.json` without editorial, licensing, asset, rendering, and
device-capacity review.

## Checked snapshot

`pokedex_source_snapshot.json` was generated on 2026-09-15 from PokéAPI's data
repository at commit:

```text
4b82c204ddd19ecb8eda2ea044ccb59e222b721c
```

The snapshot records aggregate counts, membership hashes, response hashes, and
source revision metadata. It intentionally does not redistribute official
descriptions, artwork, sounds, or complete third-party source tables.

Observed structured-source totals:

| Entity | Count |
|---|---:|
| Species | 1,025 |
| Pokémon varieties | 1,351 |
| Pokémon forms | 1,579 |
| Pokédex scopes | 35 |
| Generations | 9 |
| Species-to-Pokédex memberships | 8,010 |

These are different entities and must not be combined into one “Pokémon count.”
PokéAPI is a structured secondary source, not an official Pokémon authority.
Its form and Pokédex resource semantics are useful for ingestion engineering but
remain subject to field-level verification.

## Source roles

The complete machine-readable registry is
[`pokedex_sources.json`](pokedex_sources.json).

- The official Singapore Pokémon Pokédex verifies that National Pokédex number
  1025 is Pecharunt.
- Pokémon HOME documents National Pokédex, form registration, and game-specific
  Pokédex behavior.
- Pokémon GO help documents regional views and orthogonal category views such as
  Shiny, Lucky, and Gigantamax.
- The official Pokémon Legends: Arceus site documents research tasks as a
  progression dimension separate from simple capture.
- PokéAPI supplies versioned structured CSV data for automated census and diffing.

Official pages are reference and review sources only. Pokémon's terms state that
site content, including text and media, is protected. This repository must not
bulk-copy or redistribute official text or assets without an explicit right to
do so. PokéAPI's software/data repository has its own license and trademark
notice; retaining provenance does not grant rights to Pokémon intellectual
property.

## Refresh

Refresh explicitly:

```sh
python3 tools/sync_pokedex_research.py --refresh
```

Force a complete source download even inside the refresh interval:

```sh
python3 tools/sync_pokedex_research.py --refresh --force
```

Validate the checked-in registry and snapshot without network access:

```sh
python3 tools/sync_pokedex_research.py --check
```

Normal refreshes are rate-controlled to once per 24 hours. When due, the tool
first resolves the source repository revision. If it has not changed, the
checked-in snapshot is reused. When it has changed, the five required CSV files
are downloaded from the immutable commit URL, validated, hashed, compared with
the prior snapshot, and written atomically.

The snapshot never updates the production catalog automatically. A source change
must pass source review, field-level verification, content licensing, Host
tests, LVGL rendering, firmware size checks, and physical-device validation
before release.
