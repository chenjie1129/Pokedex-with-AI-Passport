# Charmander Capture MVP

## Product slice

This slice proves one complete loop:

```text
confirmed place
  -> Charmander encounter
  -> timing-based ball throw
  -> durable capture transaction
  -> bestiary detail
```

Only Charmander is playable. Bulbasaur and Squirtle are intentionally deferred
until this loop is accepted.

## Domain design

### Capture engine

`capture_engine` creates a deterministic 12-second round from an injected seed.
The target is placed between 4 and 8 seconds, with a 900 ms half-width. A throw
consumes the round and returns hit, miss, timeout, or invalid.

The engine owns no clock or random source. Firmware injects `now_ms` and a seed,
which keeps Host tests deterministic.

### Bestiary service

The bestiary stores:

- Charmander discovery state;
- capture count and last local place ID;
- a monotonic `last_settled_sequence` high-water mark;
- schema version and CRC-32 in its encoded form.

Capture is transactional. The service builds a copy, asks the storage adapter
to persist that copy, and mutates live state only after persistence succeeds.
Any encounter sequence at or below the durable high-water mark is a duplicate,
including events replayed long after the former 16-entry window would have
evicted them. New encounters use `last_settled_sequence + 1`, so storage remains
fixed-size while lifetime capture count remains independent. Schema v3 keeps
the encoded blob at 156 bytes, decodes valid schema-v1/v2 snapshots, and the BSP
rewrites migrated blobs canonically during load.

### Game loop

`game_loop` is the application state machine:

```text
WAITING_FOR_PLACE
  -> ENCOUNTER
  -> CAPTURE
     -> CAPTURED -> BESTIARY
     -> CAPTURE (miss or timeout, attempts remain)
     -> ESCAPED (three misses, three idle timeouts, or shared deadline)
     -> STORAGE_ERROR -> CAPTURED (retry succeeds)
     -> REWARD_ERROR (non-retryable reward rejection)
```

The encounter is currently fixed to species `004` for MVP validation.

## Browser acceptance simulator

`demo/` is a 240x320 Passport-style interaction simulator. It uses the same
states and transaction semantics, but is not the firmware implementation.
It exists so product acceptance can happen before ESP-IDF and device wiring:

```sh
python3 -m http.server 4173 --directory demo
```

Open `http://127.0.0.1:4173`, then use the center button or Enter:

1. scan and confirm Place 01;
2. enter the Charmander-only encounter;
3. switch to the first-person aim view;
4. press during the green capture zone and watch the ball travel from the
   foreground to the target;
5. open the bestiary.

The simulator persists the captured entry in `localStorage`. Open
`http://127.0.0.1:4173/?debug=1` to expose the reset button for repeated
acceptance runs.

The browser-only `throwing` presentation state does not change the domain
transaction. It gives the 240x320 Passport screen enough time to render the
first-person throw before capture persistence begins. The firmware port should
map this presentation state to an LVGL animation timer; it must not block the
LVGL task. The Poke Ball is drawn from primitives, and the 215x215 official
Pokedex artwork must be converted to the device color format during firmware
asset packaging.

## Persistent navigation and discovery states

The firmware boots into a permanent home screen with `EXPLORE` and `BESTIARY`
entries. UP and DOWN move the selection and OK opens it. The bestiary supports
an empty state, a one-row list, a detail view and an explicit path back home;
no capture is required to open it.

The discovery model has three durable states:

- `UNKNOWN`: omitted from the list;
- `SEEN`: visible with zero captures and an unknown capture place;
- `CAPTURED`: visible with lifetime count and the most recent capture place.

A first encounter calls `city_bestiary_mark_seen()` on the NVS worker before
the encounter screen is published. A failed write therefore cannot leak a
reward or discovery into the UI. Capture upgrades the same record to
`CAPTURED`; three misses return home while the durable `SEEN` entry remains
browsable. The browser simulator mirrors the same home, list, detail and
three-state transitions.

## Firmware integration boundary

The production firmware now owns a `city_bestiary_t` read model and delegates
all durable writes to `bsp_bestiary_store`. The adapter stores the 156-byte,
checksummed schema-v2 blob under `pokedex/bestiary_004`. If that blob is
missing, it imports `pokedex/caught_004`, commits the new blob and removes the
legacy key in the same NVS transaction. Legacy integer records preserve their
lifetime count without inventing encounter IDs or a place ID.

Capture settlement runs in a worker task so NVS writes never block the LVGL
task. The UI transitions to `captured` only after the domain service receives a
successful durable commit; load or commit failures fail closed. Wi-Fi scanning
remains a separate asynchronous integration boundary.

ESP-IDF 5.5 production builds and ESP32-C3 tests cover legacy migration, blob
reload after restart, permanent stale-event idempotency, storage-boundary
fault injection, and capture counts beyond the former 16-entry limit. Capture
uses three 4.2-second rounds under one 15-second input deadline; idle rounds
auto-expire instead of looping forever.
