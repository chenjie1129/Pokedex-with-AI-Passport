# Stage 3 increment 1: signed host content containers

Status: implemented host prototype, 2026-09-17. **Not a device-installable pack.**
The owner authorized Stage 3 while Stage 2 device verification remains unfinished;
see [the decision](../verification/pokedex-stage2-deferral-2026-09-17.md).

## Purpose and current boundary

Authenticate a bounded catalog/object index and detect damaged or missing assets
before any future activation step. `tools/pokedex_content_pack.py` builds and
verifies a compact offline file using Python's standard library and OpenSSL 3
with Ed25519 support. Neither command contacts a service, changes a save, flashes
a device, or installs content. Verification deliberately reports
`device_compatible: false`.

This increment treats metadata, sprites and cries as opaque objects. Their game
schema, supported glyphs, sprite/audio formats, licenses and device resource
requirements are **not** validated here. Passing container verification is not
permission to display, activate or distribute its content.

## Binary layout

All integers are unsigned little-endian. No archive extraction or embedded paths.

| Section | Encoding | Meaning |
| --- | --- | --- |
| Header (28 bytes) | `<8sHHIIQ` | Magic `CITYPK01`, version 1, algorithm 1, nonzero revision, object count, payload byte count |
| Index (44 bytes/object) | `<HBBII32s` | Stable species ID, object kind, reserved zero, payload-relative offset, byte length, SHA-256 |
| Signature (64 bytes) | Ed25519 | Signature of domain-separated manifest digest |
| Payload | Contiguous object bytes | Metadata, sprite and cry for each species in index order |

Each species has exactly three objects: 1 = metadata, 2 = sprite, 3 = cry.
Species IDs are unique ascending integers 1–65534, with object kinds ordered
1/2/3. They are identities, not row positions. Original-content ID policy and
species semantics remain the future catalog validator's responsibility.

The signed message is the exact byte sequence:

```text
ASCII("CityPassport.ContentPack.v1") || 0x00 || SHA256(header || index)
```

The index contains the payload object hashes, so the signature binds revision,
IDs, object kinds, sizes, positions and asset bytes. The verifier requires an
out-of-band trusted Ed25519 public key; a pack cannot nominate its own key.
Other key types, unknown versions/algorithms, reserved flags, duplicate IDs,
missing objects, gaps/overlaps, trailing bytes and truncation are rejected.

Ed25519 is the **host prototype** profile. Firmware crypto support, code size and
verification time must be measured before selecting the device profile. A
different encoding or signature scheme requires a version/algorithm change,
never reinterpretation of existing signed bytes. No production signing key or
device trust anchor has been generated or approved by this increment.

## Bounds and verification

- At most 30,000 objects (10,000 synthetic species in this host profile).
- Each object is nonempty and at most 1 MiB.
- Entire pack at most 8 MiB; `verify --max-bytes` can impose a smaller budget.
  This is a host rejection ceiling, not free/allocated device storage.
- The verifier reads one index entry at a time and hashes payloads in reads of
  at most 64 KiB. It does not materialize the whole catalog or payload.
- Index structure and signature are checked before payload reads. A second
  digest checks that the index used for payload verification did not change.
- The publisher may use memory proportional to index size; it runs on a host.
  It checks source bytes again while copying, writes a temporary output, then
  publishes the completed file without replacing an existing destination.

Verification describes the bytes read, not a lasting guarantee for a mutable
path. The future device installer must own immutable staging storage throughout
verification/activation, enforce revision/rollback policy, and retain the prior
working pack across errors. This tool has no activation or anti-rollback state.

## Usage

Use an existing authorized development key pair stored outside the repository.
Tests generate ephemeral keys in temporary directories and remove them. Never
commit private keys or use test keys as a production trust anchor.

A build recipe resolves paths relative to its own directory:

```json
{
  "revision": 1,
  "objects": [
    {"species_id": 1, "kind": 1, "path": "metadata.bin"},
    {"species_id": 1, "kind": 2, "path": "sprite.bin"},
    {"species_id": 1, "kind": 3, "path": "cry.bin"}
  ]
}
```

```sh
python3 tools/pokedex_content_pack.py build --recipe /private/path/recipe.json \
  --private-key /private/path/development-private.pem --output /private/path/demo.pack
python3 tools/pokedex_content_pack.py verify /private/path/demo.pack \
  --public-key /private/path/development-public.pem --max-bytes 2097152
```

## Evidence and next increments

`test_content_pack.py` covers real signature verification, wrong keys, unsigned
packs, every single-byte XOR mutation of a small pack, truncation/trailing data,
trusted signatures on malformed indexes, duplicate/missing objects, output
preservation and bounded verification of 100/1,000/10,000 synthetic species.
Synthetic payloads are nine-byte placeholders, not usable Pokémon assets.
Measured Python allocation peaks were approximately 94 KB across those three
sizes; this excludes the OpenSSL process and is **not ESP32 RAM evidence**.

Remaining Stage 3 work:

1. Define typed metadata and device image/audio formats, legal-content validation
   and firmware compatibility checks; select/measure the device crypto profile.
2. Build a bounded device package provider. The existing provider count is
   `uint8_t` and current saves use compiled catalog size; this container does not
   make 10,000 species playable or solve sparse/paged save design.
3. Allocate safe content storage without altering NVS/PHY/cardid/recovery
   contracts. Unnamed flash space is not automatically safe to erase.
4. Verify immutable staging, atomic active/rollback manifests and interruption
   recovery; implement bounded asset cache and four-row UI paging.
5. Measure a real-asset pack, firmware size, workload memory and offline device
   behavior. Resume deferred Stage 2 checks before full device acceptance.

No new collectible species, firmware changes, partition changes or cloud service
are part of this increment.
