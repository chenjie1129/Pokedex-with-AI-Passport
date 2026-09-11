# Passport progress

Home now has Explore, Bestiary, and Passport. UP/DOWN wraps through these three
items; OK opens one. Passport shows six anonymous place stamps per page,
discovered/captured species totals, and one next goal. UP/DOWN wraps pages; OK
returns Home. Screen-off/wake and battery behavior remain shared with other
static pages.

A stamp is a confirmed saved place, not a capture, scan, or Wild encounter.
Existing place IDs are displayed as PLACE 01, PLACE 02, etc., in ascending order.
No geographic names, wireless identifiers, dates, or visit counts are invented.
The last page has blank slots if fewer than six stamps remain.

The coordinator exports only IDs and count from its committed catalog while no
scan is in flight. The UI is its single caller; final-result queue delivery
precedes the UI clearing in_flight. The scan worker cannot mutate the catalog
again until the UI requests another scan. Reads do not access flash or scan Wi-Fi.
Failed place saves never enter that committed catalog. Collection counts use the
validated, persisted bestiary; repeated catches do not inflate species progress.
There is no new save format or migration.

Goals follow a fixed order:

1. Catch the first spirit.
2. Confirm the first and second places.
3. Find/catch the first uncollected species in catalog order.
4. Explore 3, 5, 10, then 16 saved places.
5. Show completion when all three species and 16 stamps are collected.

Missing or invalid place/collection data is shown as unavailable. It cannot be
mistaken for an empty collection or a completed goal. Goals award no separate
reward and need no wall clock or additional persistence.

## Verification

`./tools/test-host.sh` includes passport tests for noncontiguous/sorted IDs,
duplicate/invalid IDs, unavailable data, all capacities 0–16, pagination wrap,
failed-save visibility, Wild invariance, species counts, and goal transitions.

For a headless render using the production Home/Passport LVGL functions:

```sh
cmake -S tests/passport_render -B build/passport-render
cmake --build build/passport-render --parallel
(cd build/passport-render && ./passport_render)
```

This uses the checked-out LVGL dependency under managed_components. The generator
extracts the actual rendering functions from main/main.c, supplies fixture data,
and checks every label for clipping and screen bounds. It writes 240x320 PPMs
for Home, empty/two-place/full Passport, and unavailable data. This verifies the
rendering code, not physical buttons, scan timing, or flash hardware.

Device acceptance: open Passport from Home, verify existing stamps and collection
counts, return and reopen, confirm another place, then reboot and verify its new
stamp persists. A Wild encounter or repeated visit must not add a stamp. With more
than six places, page forward/backward and verify the final partial page.
