# Stage 3 increment 2: typed content and portable bounded reader

Implemented 2026-09-17. Host-verified and ESP32-C3 cross-compiled; **not wired
into gameplay or installed on hardware**. Stage 2 device checks remain deferred.

The original signed container remains version 1. Its opaque objects can now use
the typed profiles below. Old synthetic containers still pass generic container
verification but fail typed-reader validation; they are not silently reinterpreted.
`tools/encode_pokedex_content.py` prepares objects and a recipe for the existing
signing tool. `content_pack.c` is the portable C consumer, built in `city_domain`.

## Metadata profile

The metadata object is little-endian `<4sHHH8B4H` followed by four UTF-8 strings:

| Field | Contract |
| --- | --- |
| Magic / profile | `CM01`, uint16 profile version 1 |
| Species ID | uint16; must match the signed index ID |
| Evolves from | uint16; zero for base species, otherwise an existing base ID in this pack |
| Flags | bit 0 Wild eligible, bit 1 place eligible, bit 2 original content; all other bits zero |
| Pool | 0–2, existing encounter weighting category |
| Primary / secondary type | Stable IDs 1–18 in the order below; secondary may be 0, no duplicate types |
| HP / Attack / Defense | uint8, each 1–240 |
| Reserved | one zero byte |
| String lengths | Four uint16 byte lengths; no encoded NUL terminators |
| Strings | English name, Chinese name, English description, Chinese description |

Type order: Normal, Fire, Water, Electric, Grass, Ice, Fighting, Poison, Ground,
Flying, Psychic, Bug, Rock, Ghost, Dragon, Dark, Steel, Fairy.

String byte limits are 32/48/90/180 respectively; every string is nonempty.
Malformed/overlong UTF-8, surrogate code points, values above U+10FFFF and C0/C1
control characters are rejected. The decoded C structure adds NUL terminators.
UTF-8 validity does not guarantee available font glyphs or screen fit: glyph and
render validation remains an activation gate.

IDs 60000–65534 require the original-content flag; lower IDs forbid it. This is
namespace validation, **not a license approval**. Evolution entries cannot be
encounter-eligible. This profile supports one evolution step from a base, rejects
missing parents, self-links, cycles and multi-step chains. Expanding that policy
needs a new validated profile. No evolution changes are applied to player saves.

## Sprite and cry profiles

Sprite: 12-byte `<4sHHB3x` header with magic `CS01`, width/height 1–110,
format 1, and zero reserved bytes. Payload is a little-endian RGB565 plane
followed by one A8 alpha plane, row-major: exactly width × height × 3 bytes.
This matches the current generated RGB565A8 pixel layout; the future renderer
must provide its LVGL descriptor and buffer/cache ownership. One object contains
one image; a second small image, animation and compression are not in this profile.

Cry: 12-byte `<4sHBBI` header with magic `CA01`, sample rate 16000, channels 1,
encoding 1 (signed 16-bit little-endian PCM), and 1–32000 samples (up to 2 s).
Payload is exactly samples × 2 bytes. Every current cry fits that sample limit.
Format validation does not certify loudness, non-silence or subjective quality.

## Preparation workflow

Input JSON example (asset paths relative to the JSON file):

```json
{
  "revision": 1,
  "species": [{
    "species_id": 60000, "evolves_from": 0, "flags": 4, "pool": 0,
    "types": ["Grass"], "hp": 45, "attack": 40, "defense": 50,
    "name_en": "Mossbit", "name_zh": "Mossbit",
    "description_en": "A content fixture.", "description_zh": "A content fixture.",
    "width": 110, "height": 110,
    "sprite_rgb565a8": "mossbit.pixels", "cry_pcm": "mossbit.pcm"
  }]
}
```

These sample strings are placeholders, not approved localized player content.

```sh
python3 tools/encode_pokedex_content.py /private/path/source.json /private/path/objects
python3 tools/pokedex_content_pack.py build --recipe /private/path/objects/recipe.json \
  --private-key /private/path/development-private.pem --output /private/path/typed.pack
python3 tools/pokedex_content_pack.py verify /private/path/typed.pack \
  --public-key /private/path/development-public.pem
# After the host build, exercise the exact C typed reader with host crypto:
build/host/content_pack_reader_cli /private/path/typed.pack /private/path/development-public.pem
```

The preparation directory must be new. A preparation error can leave partial
objects; no signing recipe is published until all objects are prepared. Never
use the generic verifier result as a substitute for the typed reader or the
future firmware/font/license compatibility gates.

## Reader and trust boundary

`city_pack_open` takes bounded random-access read and crypto callbacks. It clears
its output on any error, authenticates the complete index before reading any
payload, checks all object SHA-256 values, then checks typed fields and links.
No partially verified handle is exposed. Missing callbacks and crypto/I/O
failures are failures, not an unsigned fallback.

The host adapter uses OpenSSL 3 SHA-256 and real Ed25519 verification. The device
adapter is **unfinished**: Ed25519 support and resource cost have not been
validated against the installed ESP-IDF 5.5.3 runtime. Choose and measure a supported
implementation or explicitly version a different signature profile before
device integration. Do not substitute a callback that returns success.

Storage must remain immutable from open until the reader is discarded. Future
staging/activation code owns this guarantee and the trusted key. The reader
does not itself lock flash, pin a revision, or provide rollback protection.

`city_pack_at` reads one row; `city_pack_find` binary-searches stable IDs without
loading the catalog. Both copy text into caller-owned buffers, leave output
unchanged on failure and require a verified handle. Asset descriptors and bounded
asset reads expose signed sprite/cry objects without allocating whole assets.
Call this API from workers; opening a large pack is not a UI-thread operation.

The existing `city_catalog_provider_t` is unchanged: it promises permanent
pointers and an 8-bit count. This new reader deliberately does not violate those
contracts. Adapting gameplay, current compiled-count saves and sparse progress to
package catalogs remains separate work.

## Verification results and limits

- Full host suite: 37 targets passed with sanitizers; reader tests use the actual
  production C source and a real OpenSSL crypto adapter.
- Signed malformed text, asset headers, missing/cyclic evolution references,
  wrong keys, absent/failing crypto callbacks, I/O failure, truncation,
  corruption and out-of-bounds access are rejected.
- 100/1,000/10,000 typed synthetic entries passed complete verification, four-row
  iteration and lookup. Host handle: 40 bytes; decoded row: 366 bytes, four rows
  1,464 bytes. The maximum-size sprite/cry case exercised 512-byte reads.
- ESP-IDF 5.5.3 firmware build passed. The module is compiled into the domain
  library but unused by gameplay and may be removed from the final linked image.
  This is compiler integration, not device execution evidence.
- ESP32-C3 GCC 14.2.0 `-Os -fstack-usage`: open frame 832 bytes, metadata frame
  912 bytes, read wrapper 16 bytes. Largest estimated reader call path is 1,760
  bytes, **excluding callback/crypto/storage stack and runtime task overhead**.
- Firmware candidate is a dirty development build; no flash or runtime device
  test was performed. No player save or protected partition was changed.

Next: select/measure device crypto and trusted-key handling, define protected
staging storage and cache ownership, then add atomic activation/rollback and
gameplay integration. Stage 3 is still in progress; signed host and typed-reader
tests do not prove device capacity, physical interruption recovery or full
catalog playability.
