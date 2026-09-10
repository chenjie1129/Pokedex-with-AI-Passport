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
- a bounded encounter-ID ledger for idempotency;
- schema version and CRC-32 in its encoded form.

Capture is transactional. The service builds a copy, asks the storage adapter
to persist that copy, and mutates live state only after persistence succeeds.
Duplicate encounter IDs return success-equivalent idempotency without writing
or incrementing the count.

### Game loop

`game_loop` is the application state machine:

```text
WAITING_FOR_PLACE
  -> ENCOUNTER
  -> CAPTURE
     -> CAPTURED -> BESTIARY
     -> CAPTURE (miss, attempts remain)
     -> ESCAPED (three misses)
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

## Firmware integration boundary

The next firmware slice must provide adapters for:

- Wi-Fi observations into the W1 location engine;
- monotonic time and random seed;
- bestiary blob storage through NVS;
- three button events and LVGL rendering;
- asynchronous Wi-Fi and NVS work outside the LVGL task.

No firmware build or physical-device claim is made by this MVP because the
current development environment does not contain ESP-IDF.
