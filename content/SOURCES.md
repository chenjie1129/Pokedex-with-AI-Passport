# Species information

Type and description references were checked on 2026-09-12 against each
species' official Singapore Pokémon Pokédex entry. `info_source` in
`species.json` links to the exact page for every entry. Descriptions are short,
authored paraphrases of those pages, sized for the 240×320 detail screen.
They describe the species, not additional mechanics implemented by this game.

`types` contains encyclopedia types for the depicted standard form, including
both types where applicable. `element` remains the existing simplified game
category. Adding display types does not change encounter weights or stats.
In particular, the standard Jigglypuff entry is Normal/Fairy, Bulbasaur,
Ivysaur and Oddish are Grass/Poison, Gastly is Ghost/Poison, and Geodude is
Rock/Ground.

Catalog version 4 updates read-only content. No save schema or species IDs
change. The generator validates content bounds and regenerates the C table;
the production LVGL render harness checks that wrapped facts fit their height
as well as their width across seen, caught and evolved fixtures.

Artwork provenance remains in `demo/assets/SOURCES.md`.

## Stage 2 original candidate: Mossbit

Mossbit (stable local ID 60000) is an original migration-test creature, not a
National Pokédex entry. Its authored description, pixel artwork and synthesized
cry are created for this project and provided under CC0-1.0. Reproduce assets
with `tools/generate_mossbit.py`; no external image or recording is used.
It is catalog-visible but excluded from both place and wild encounter pools.
Existing third-party Pokémon asset rights are unchanged.
