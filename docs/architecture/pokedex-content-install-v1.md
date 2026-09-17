# Stage 3 increment 3: device crypto and transactional content storage

Implemented 2026-09-17. **Host-tested and ESP32-C3 cross-compiled, not installed
or connected to gameplay.** Stage 2 device verification remains deferred.

This increment adds the actual mbedTLS device verification adapter, a guarded
ESP partition adapter and a portable two-slot installer with journaled activation
and rollback. Default `partitions.csv`, player saves, recovery and device identity
are unchanged. No production trust anchor or signing key has been provisioned.

## Explicit P-256 signature profile

The version-1 container now accepts algorithm **2**, ECDSA P-256 / SHA-256. The
signed message remains `ASCII("CityPassport.ContentPack.v1") || 0x00 ||
SHA256(header || index)`. ECDSA hashes that message with SHA-256, then signs the
result on secp256r1. Its 64-byte signature is unsigned big-endian `r[32] || s[32]`,
not ASN.1 DER. Zero/out-of-range scalars and invalid public points are rejected by
mbedTLS. Both mathematically valid low/high-S signatures are accepted; signatures
are not used as package identity. The authenticated manifest digest is the identity.

Algorithm 1 retains the existing Ed25519 host profile. There is no reinterpretation
of old signatures. The algorithm itself is inside the signed header. Unknown
algorithms fail; the production BSP adapter accepts only algorithm 2 because the
installed ESP-IDF mbedTLS configuration has P-256 but no Ed25519 verifier.

`pokedex_content_pack.py build --algorithm p256` requires a P-256 key, and
`verify` selects the exact algorithm from the header. The publisher uses OpenSSL
and strictly converts its DER signature to the wire representation. Default build
behavior remains Ed25519 for backward compatibility. The generic host verifier
still reports `device_compatible: false`: signature validity alone does not
approve fonts, licensing, storage capacity or gameplay compatibility.

`bsp_content_crypto_init` accepts an out-of-band **65-byte uncompressed P-256
public point**. A pack cannot supply a trust key. Missing/wrong-sized/off-curve
keys fail closed. There is no default/test key compiled into production. Key
approval, provisioning, rotation and recovery remain delivery work. One worker
owns the initialized context and frees it after use. mbedTLS may allocate during
verification; physical heap, stack and latency measurements are still pending.

## Storage contract: disabled on the current device layout

`bsp_content_storage_open` enumerates and validates the whole partition table.
It requires all five existing protected entries, plus exactly these optional
future entries on the same flash chip:

| Label | Type/subtype | Address | Size |
| --- | --- | --- | --- |
| content_a | data / 0x40 | 0x360000 | 0x1a0000 (1,703,936 bytes) |
| content_b | data / 0x40 | 0x500000 | 0x1a0000 |
| content_ctl | data / 0x41 | 0x6a0000 | 0x2000 (two erase sectors) |

The five existing entries must match their current names, types, addresses and
sizes exactly. Extra/duplicate/missing entries, flags, differing chips or changed
bounds fail. Reads/writes use only `esp_partition_*`; there is no raw-flash fallback.
Data reads/writes are at most 512 bytes. Each selector-bank erase affects only its
own 4 KiB sector. All adapter errors propagate.

**This is a future-layout contract, not permission or tooling to repartition.**
Current firmware and backup checker retain the five-entry layout, so opening
content storage currently returns unavailable without flash I/O. Unnamed flash
has not been inspected or declared free. A separately reviewed migration must
back up and audit the device, preserve protected bytes and recovery compatibility,
then update provisioning and backup tooling before this adapter can be enabled.
No alternate flashable partition table is supplied in this increment.

## Worker ownership and update sequence

`city_content_store_t` lives at a stable address, owned by a single serialized
worker. Its backend and crypto contexts must outlive it. Boot/reconciliation runs
before handing out readers. No UI, audio or other task may call its mutation
callbacks directly or reinitialize/copy a live manager. This API is not a mutex;
worker dispatch still needs integration.

1. Boot reads both activation records and checks CRC, structure, capacity and
   generation. It reauthenticates and fully validates each candidate pack before
   selecting the newest usable one. An unreadable record aborts boot; an invalid
   record/pack is skipped. With no usable installed pack, `active_slot=-1` tells
   the future application to use its compiled catalog. It does not modify saves.
2. Install rejects outstanding reader leases, exhausted generation, over-capacity
   data, and revisions at/below the surviving revision high water. It fully
   validates the source before erasing the inactive slot.
3. Copy into the inactive slot with bounded reads/writes, then independently
   authenticate and validate the stored bytes. The source must remain immutable;
   its manifest digest must match the staged digest.
4. Erase the other selector bank, write its new activation record, then read it
   back and compare every byte. Publish the new active handle only after success.
5. A selector I/O failure is potentially an ambiguous commit. The manager becomes
   unavailable until boot reconciliation. A complete durable record may be selected
   on reboot even if the original write returned an error. An incomplete record
   leaves the previous verified pack selectable.

The previous active pack is never overwritten during installation. **Beginning
another update consumes the older rollback slot.** A failed staged copy can lose
that older fallback, but preserves the currently active pack. This is two-slot
storage, not an unlimited history.

Explicit rollback authenticates the other recorded pack and writes a newer
selector generation pointing to it, retaining the highest revision. A subsequent
normal install cannot silently reinstall that older revision. Rollback can also
switch forward to the other retained pack. Corrupt/missing previous data rejects
rollback. Generation `UINT32_MAX` rejects further mutations instead of wrapping.

The record is 80 bytes: magic `CITYACT1` (8), generation/slot/pack bytes/revision/
high revision (five little-endian uint32), manifest SHA-256 (32), zero reserved
bytes (16), IEEE CRC-32 over the first 76 bytes (4). The selector is a corruption
journal, **not tamper-proof anti-rollback storage**. If both records are lost,
the application falls back to compiled content and the high water is lost.
Physical hostile-flash rollback prevention is outside this contract.

Readers use balanced acquire/release leases. An install or rollback with any
outstanding lease fails without mutation; handles and asset descriptors cannot
outlive their lease. Renderer/audio cache ownership, cancellation and eviction
are still to be wired to this API.

## Verified evidence

- Full host suite: **39/39**, sanitizers enabled on project C code. Host mbedTLS
  3.6.5 was built from the installed ESP-IDF source; its library itself was a
  Release build. CI additionally exercises Ubuntu's mbedTLS development package.
- Real OpenSSL-signed P-256 typed packs pass the exact BSP mbedTLS verifier and
  installer. Every byte of a fixture is separately corrupted and rejected; wrong
  keys, off-curve keys, scalar extremes, truncation, trailing bytes and algorithm
  confusion fail. Ed25519 host compatibility tests still pass.
- Installer tests inject **21 I/O failures** and **1,802 torn mutations** across
  staging, journal erase/write and rollback. Reboot chooses the old or fully
  committed new pack; active-slot bytes remain identical. Tests also cover silent
  write corruption, held leases, stale revisions, corrupt-pack fallback, compiled
  fallback, ambiguous commit and generation exhaustion. These are simulated NOR
  operations, not physical power interruption or flash timing evidence.
- Actual BSP partition-adapter source runs against host partition API fixtures:
  exact current layout rejects with zero flash I/O; bad layouts, bounds, sector
  isolation and driver errors are checked.
- ESP-IDF 5.5.3 / ESP32-C3 build passed; the new sources compile into BSP/domain
  archives. They are unused by gameplay and may be removed from the final image
  by the linker. Image size remains `0x2b4fd0`, fitting the 3 MiB factory slot;
  this does **not** measure the fully linked install feature's flash cost.
- No flash, USB operation, physical test, catalog activation or save migration.

## Remaining before players can use packs

1. Approved trust key/provisioning and a reviewed storage migration with backup
   and recovery validation; measure fully linked code, runtime heap/stack/latency.
2. Package provider and bounded cache integrated with UI/audio worker ownership.
3. Sparse progress/save migration and validation of owned/buddy/evolution IDs
   across activation and rollback. The installer currently validates pack contents,
   not compatibility with an existing player's saves.
4. Glyph/render/licensing and real content-capacity checks, then offline gameplay,
   physical interrupted updates and long-running device acceptance.

No additional collectible Pokémon are enabled by this infrastructure increment.
