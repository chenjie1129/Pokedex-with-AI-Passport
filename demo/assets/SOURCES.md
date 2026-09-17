# Image Sources

All Pokemon artwork used by the demo must come from the official Pokemon
Pokedex:

- Source page: https://www.pokemon.com/us/pokedex
- Charmander (`charmander.png`):
  https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/004.png
- Bulbasaur (`bulbasaur.png`):
  https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/001.png
- Squirtle (`squirtle.png`):
  https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/007.png

UI scenery and the Poke Ball are drawn at runtime and are not bitmap assets.

These assets are for the controlled prototype only. Public or commercial
distribution requires original City Spirits artwork.

## Twelve-species device roster

- Pikachu: https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/025.png
- Jigglypuff: https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/039.png
- Oddish: https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/043.png
- Meowth: https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/052.png
- Psyduck: https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/054.png
- Growlithe: https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/058.png
- Geodude: https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/074.png
- Gastly: https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/092.png
- Eevee: https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/133.png

Generated device pixels retain source attribution in roster_sprites.c.

Evolution artwork (same official prototype source):
- Ivysaur: https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/002.png
- Charmeleon: https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/005.png
- Wartortle: https://www.pokemon.com/static-assets/content-assets/cms2/img/pokedex/full/008.png

## Stage 2 original candidate: Mossbit

Mossbit (stable local ID 60000) is an original migration-test creature, not a
National Pokédex entry. Its authored description, pixel artwork and synthesized
cry are created for this project and provided under CC0-1.0. Reproduce assets
with `tools/generate_mossbit.py`; no external image or recording is used.
It is catalog-visible but excluded from both place and wild encounter pools.
Existing third-party Pokémon asset rights are unchanged.
