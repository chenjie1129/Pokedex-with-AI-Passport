# Stage 3: offline signed content on the Passport

## Player and publisher behavior

A compatible content pack supplies the Pokédex's names, descriptions, sprites,
cries, encounter eligibility and species stats. It stays fully usable offline.
An owner installs packs over USB; the device verifies the signature, every object,
font coverage, text bounds and save compatibility before activating one at boot.
No server, account or phone is needed for play.

The built-in 16-species catalog remains the fallback. Existing Pokémon keep their
individual IDs, stats, HP, personality, friendship, memories and buddy selection.
A missing package that is required by a save causes a load error; it never resets
or silently discards those Pokémon. Updates must retain the built-in species and
the mechanics/evolution targets of discovered package species.

The list creates at most four rows and uses 32-bit cursors, including beyond row
255. Package-only species follow the existing capture, individual companion,
health, release and evolution rules. The host renderer exercises 1,000-entry list
boundaries in both languages. Full encounter selection runs on a worker, with
input locked until it publishes a result.

## Fixed resource bounds

- A player model is 6,952 bytes on the tested host and ESP32-C3 builds.
- It retains 16 baseline progress records plus up to 48 discovered package IDs;
  the 160-owned-copy limit is unchanged. These bounds are independent of catalog
  size. Releasing the last copy retains discovery history, rather than reclaiming
  its progress record. A full collection produces explicit capacity feedback.
- Storage format 13 remains unchanged: discovered stable-ID records and actual
  owned copies only. The decoder now resolves package IDs against the boot-selected
  catalog. Legacy format-12 canonical encoding refuses package progress rather
  than omitting it; production NVS uses the sparse encoder and exact length.
- UI and audio each own a separate four-row/four-block view. Asset blocks are 512
  bytes; audio copies at most 256 PCM samples per read. Readers never share mutable
  caches. A small UI definition ring provides copies for the existing renderer.
- The verified active partition is mapped read-only after all boot mutations.
  Runtime lookups copy immutable mapped bytes: they perform no blocking partition
  calls, verification, writes or catalog-sized allocations on the LVGL task.
  Two reusable sprite descriptors point into immutable mapped pixels; reuse drops
  LVGL image/header cache entries before changing a descriptor.
- Views hold leases for the entire runtime. Installation and rollback are boot
  worker operations; USB tooling resets the device before staging content.

The container supports up to 10,000 entries, tested on the host. Physical pack
capacity is **1,310,720 bytes per slot**; asset sizes determine the usable count.
A 1,000-entry synthetic pack is a resource test, not 1,000 newly authored Pokémon.
The current 16-species pack occupies 1,021,188 bytes, leaving 289,532 bytes per
slot for additional metadata/media. The demonstrated extra full-size species
costs 46,158 bytes; that asset size would allow six more entries in this layout.

## Storage migration and protection

The initial proposal used space containing legacy bytes at `0x3fa000–0x419908`
on the connected Passport. The shipping extension instead uses verified-empty
regions and leaves that occupied space intact:

| Partition | Offset | Size |
|---|---|---|
| content_a | `0x420000` | `0x140000` |
| content_b | `0x560000` | `0x140000` |
| content_ctl | `0x6a0000` | `0x2000` |

All five legacy partition addresses/sizes, bootloader, NVS, PHY, card identity and
recovery remain unchanged. The partition-table extension is a distinct operation
from an ordinary app-only update. The planner accepts only the exact supported
legacy/extended layouts, verifies the new table MD5, and rejects occupied proposed
slots. The installer takes a fresh 8 MiB backup and reads the complete flash back
before boot, comparing every byte outside the planned erase sectors.

A partial, corrupt, incompatible or wrongly signed inactive slot cannot switch
active content. Activation uses the existing two-bank journal and durable readback.
Beginning another update consumes the older rollback slot while preserving the
currently active slot. Boot authenticates journaled candidates and falls back to
an older compatible pack if the newest one is damaged. Host fault tests also
exercise torn activation/rollback journal operations and ambiguous commits.

## Trust and publishing

The public P-256 owner key is in `content/trust/owner-p256-public.pem` and the
matching raw point/header. Only the public key is committed. The corresponding
private signing key is local in Git-ignored `secrets/content-signing.pem`, mode
0600. Retain a private backup: losing it prevents signing updates for this firmware.
Key replacement requires a new reviewed firmware image; there is no unsigned or
in-package trust override. These are integrity controls, not secure-boot or
hostile physical-flash anti-rollback guarantees.

Build a pack from the existing exact generated artwork and hash-verified cries:

```sh
python3 tools/build_builtin_pack.py /private/path/pack-build \
  --private-key secrets/content-signing.pem --revision 13
```

For additional authored species, use `tools/encode_pokedex_content.py`, then
`tools/pokedex_content_pack.py build --algorithm p256`. Packs must include the
baseline IDs. The platform rejects unavailable glyphs and names/descriptions that
exceed the current screen layout. Asset licensing and authored quality remain
content-release responsibilities; generated test packs are private diagnostics.

Inspect an installation plan from this device's backup:

```sh
python3 tools/plan_content_install.py /private/path/full-flash.bin \
  --partition-table build/partition_table/partition-table.bin \
  --content /private/path/pack-build/content.pack
```

Install using ESP-IDF's Python environment with esptool and pyserial:

```sh
python tools/install_content_usb.py --port /dev/cu.usbmodemNNNN \
  --content /private/path/pack-build/content.pack \
  --backup-directory device-backups/a-new-directory
```

For a legacy five-partition device, also supply the tested `--firmware` and
`--partition-table` paths. A normal subsequent content update does not need a
firmware change. The tool verifies the host signature, reads back programmed and
protected bytes, and requires the requested revision and player save to become
ready in the boot log. It refuses damaged recorded packs rather than guessing
which slot the device will use. Preserve the backup on any error.

The domain's explicit rollback API authenticates and checks save compatibility.
The USB publishing workflow can also restore prior content as a newly signed,
higher revision; it never lowers the high-water revision.

## Evidence boundary

Stage 3 automated acceptance covers host faults, production-derived renders,
ESP32-C3 compilation, connected-device verification/activation, package-only
in-memory gameplay and serialization, isolated real NVS persistence across reboot,
protected-byte readback, and a sustained
no-player-save workload. This does not turn Stage 2's deferred physical buttons,
subjective audio, carry, power-loss or two-hour interactive acceptance into passes.
See the final device report for exact tested commits, images, timings and scope.
