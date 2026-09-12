# Third-Party Notices and License Scope

## Project license

The MIT License in [LICENSE](LICENSE) applies only to original source code and
documentation whose copyright is owned by Chenjie Fang or other project
contributors.

It does not grant rights to third-party names, trademarks, character designs,
artwork, photographs, videos, fonts, hardware designs, or other material.

## Restricted prototype content

The current development revision contains Pokemon-related prototype content,
including:

- `demo/assets/bulbasaur.png`
- `demo/assets/charmander.png`
- `demo/assets/squirtle.png`
- `main/assets/charmander_sprite.c`
- `main/assets/starter_sprites.c`
- character names and related references in source code and documentation
- screenshots, videos, cover images, and firmware binaries rendered from that
  content

The bitmap files were obtained from URLs documented in
`demo/assets/SOURCES.md`. The generated C arrays are transformations of those
bitmaps. These materials are not licensed under this project's MIT License,
and this project does not grant permission to copy, modify, or redistribute
them.

Pokemon and related character names are trademarks of their respective
owners. This is an independent prototype and is not affiliated with,
authorized, sponsored, or endorsed by Nintendo, The Pokemon Company, Game
Freak, or Creatures.

A non-commercial or fan-made label is not a substitute for permission from
the relevant rights holders. Remove or replace all restricted content before
publishing a redistributable community release.

## Reused foundation

Parts of the hardware foundation are derived from:

- Bubu Passport / MayDayFansInTraePassport
- <https://github.com/chenjie1129/MayDayFansInTraePassport>
- Copyright (c) 2026 FoloToy
- [MIT License](LICENSES/FoloToy-MIT.txt)

The upstream copyright and permission notice must remain with copies or
substantial portions of that software.

## Build dependencies

The dependency versions used by the firmware are pinned in
`dependencies.lock`, including ESP-IDF, LVGL, and Espressif components. They
are maintained by their respective authors and remain subject to their own
licenses. They are not relicensed by this project.

Before distributing a firmware binary, collect and include the license notices
required by the exact dependency versions used for that build.
