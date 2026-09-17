# Stage 3 increment 4: bounded catalog views and save compatibility

Implemented 2026-09-18. **Host-tested, ESP32-C3 cross-compiled; application UI,
audio and encounter loops still use the compiled catalog.** No content pack was
installed or activated on hardware. Stage 2 physical verification remains deferred.

## What this connects

`catalog_view` adapts the existing compiled definitions and an installed,
verified `content_pack` to the same caller-owned metadata/page API. The installer
now requires an application compatibility policy, and the production schema-12
policy checks the real bestiary data model before permitting activation.

This is a domain integration increment, not a larger playable roster. The
existing permanent-pointer `city_species_definition` API stays intact while
future package consumers move to copies and explicit lifetime ownership. Save
schema 12, its wire format and the shipping partition table remain unchanged.

## Catalog and cache contract

- `city_catalog_open_compiled` preserves compiled iteration order. Its two type
  IDs are generated from the same stable type list as the pack encoder. The
  compiled row's Chinese fields contain English fallback; the existing
  `ui_strings` localization adapter remains necessary for compiled content.
  Pack rows retain their authored English/Chinese strings.
- `city_catalog_open_installed` acquires a lease on the active verified package.
  Each open view pins the pack until `city_catalog_close`; installation and
  rollback reject outstanding leases. Views must be zero-initialized, remain at
  stable addresses and belong to the same serialized content worker as the
  installer. Close before switching source; close clears every cached entry.
- Counts and cursors are 32-bit, independent of the legacy 8-bit provider. Stable
  species IDs remain 16-bit identities, not row positions.
- The view retains **four metadata rows** and **four 512-byte asset blocks**.
  Least-recently-used eviction has bounded rank values, with no ticking counter
  that can overflow. Access returns copies; neither UI nor audio retains a cache
  pointer that could be overwritten by another lookup.
- `city_catalog_page` returns at most four rows. Invalid page arithmetic and
  out-of-range IDs fail. When the count is divisible by four, a zero-row page at
  the exact end is valid so a caller can display its Back row.
- Pack asset reads include the typed 12-byte header and are limited to 512 bytes
  per call. A request may cross two cached blocks. It publishes output only
  after all required reads succeed; a failed second read leaves caller output
  unchanged. Successfully read blocks may still be retained after that failure.
- Compiled assets keep their existing static sprite/cry adapters; this view does
  not pretend they have the pack byte encoding. No whole image or catalog is
  allocated by the cache. Full-frame pixel buffers and audio buffering remain
  separate resources that the application must budget.

All package I/O must run on a worker. This API does not add a FreeRTOS queue,
mutex, LVGL objects, glyph checks or audio scheduling. The application must copy
prepared page data under its UI lock and coordinate cancellation/close with
asset users before an update. Opening the view on the UI thread and letting a
cache miss block rendering would violate the intended integration contract.

## Save compatibility is now mandatory at activation boundaries

`city_content_boot` takes a required `city_content_policy_t`. Missing policy is
an error. Signature/typed validation remains mandatory and precedes the policy.
The policy runs for each boot candidate, before erasing an inactive slot, again
before committing activation, and for rollback. If no acceptable installed pack
remains, the policy must separately accept the compiled fallback (`candidate=NULL`).
A rejected fallback leaves the store unavailable, rather than silently clearing
player progress or claiming a safe default.

`city_content_accept_legacy_save` supports the **current schema-12 compiled-roster
save**, using the existing full bestiary validator. During a serialized content
operation its context must be an immutable, current save snapshot. It requires:

1. Every discovered species to remain readable by stable ID, including retained
   discovery history after release.
2. Existing base HP/attack/defense, primary/secondary type and evolution parent
   to match. Text and encounter eligibility can change without rewriting owned
   stats or identity.
3. An evolved species' known parent to remain compatible, and the existing next
   evolution of each captured species to remain available and compatible.
4. Owned-instance IDs, buddy selection, HP, friendship, memories and all other
   save invariants to pass the existing schema-12 validator. The policy never
   mutates or re-encodes the save.

Rollback is rechecked against **current progress**, not the progress that existed
when the old pack was installed. For example, an earlier pack containing only
Pikachu cannot become active after the player captures Bulbasaur. If the newer
pack is damaged, the current schema-12 save can still use its compiled roster;
if that save is invalid, boot fails instead of manufacturing replacement data.

This is deliberately not a schema migration. Unknown entries need no save slot
merely to browse package metadata, but the game cannot capture package-only
species yet. Sparse discovery storage, owned-ID validation against package
catalogs, migration/commit/readback and future fallback semantics require the
next save-model increment. The legacy policy must not be reused unchanged for
that future format.

## Evidence and limits

- **40/40 host tests passed**, with project C sanitizers enabled. Existing capture,
  owned-copy, buddy, evolution, navigation, storage and localization tests pass.
- The actual catalog view runs over real signed typed packs with 100, 1,000 and
  10,000 synthetic entries. Page iteration, stable-ID lookup, LRU eviction,
  caller-copy retention, lease rejection, source switching and read failures pass.
- Maximum-size assets exercise block eviction and a failed read across a block
  boundary. Invalid IDs, kinds, page numbers and asset bounds are rejected.
- Actual OpenSSL P-256 publisher → mbedTLS BSP verifier → installer → catalog view
  → legacy save policy passes. Signed incompatible packs with changed stats/types,
  missing owned species or missing required evolution are rejected before flash
  I/O. Capture after installation blocks an incompatible rollback; reboot checks
  save compatibility again. The save remains byte-identical across rejected
  updates, including duplicate owned copies, buddy and damaged HP.
- Host view size **3,664 bytes**, independent of catalog count. A caller-owned page
  is **1,476 bytes**, separate from the view; these figures exclude the manager,
  crypto allocations, worker stack and rendering/audio buffers.
- ESP32-C3 GCC `-fstack-usage` reports page frame 1,520 bytes, row lookup frame
  400 bytes, asset-read frame 576 bytes and block-load frame 544 bytes. These are
  individual frames, not measured worst-case task stack; nested reader, crypto,
  storage and task overhead still need physical measurement.
- ESP-IDF 5.5.3 firmware build passes. Development image size `0x2b5010` fits the
  3 MiB factory slot. New view/compatibility sources compile into the domain
  archive but are not called by the application and may be removed by the linker.
  This does not measure the fully connected feature's flash or runtime cost.
- No UI rendering changes, new collectibles, device flash, save migration,
  physical power-interruption test or Stage 2 acceptance claim.

## Next

Implement bounded sparse progress and its durable migration before enabling
package-based captures. Then connect the catalog/page/asset copies to UI, audio
and encounter workers, validate localization/rendering and resources, and finish
trust-key provisioning plus a reviewed storage migration. Device tests and the
remaining Stage 2 verification debt stay explicitly unfinished.
