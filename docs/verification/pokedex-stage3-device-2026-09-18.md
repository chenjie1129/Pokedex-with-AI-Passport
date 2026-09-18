# Stage 3 engineering acceptance — 2026-09-18

## Decision and scope

**PASS: Stage 3 signed content packages and bounded offline runtime.** The owner
explicitly requested autonomous completion, all tests, publication of all local
changes and connected-device verification on 2026-09-18. This report records that
engineering acceptance. Stage 2 remains `verification_deferred`, unfinished;
physical buttons, subjective sound, carry experience, real power interruption
and the two-hour interactive soak have not acquired a pass. Stages 4–6 stay blocked
by the product/capacity gates. No cloud services were added.

## Published implementation and final device

- Normal firmware source commit: `134631dd6fc9101773f7af7ca562f08274f04b28` (clean build).
- Source snapshot SHA-256: `1f520b20272d68891a6143a46f13daa6215eb248375debcb14cbcc25ca4fc48c`.
- Firmware SHA-256: `ff238f1d2e45797359d725d15d2a149fe779ef9bf6e88f95e47bcc6b6049811d`.
- Application size: **2,854,272 bytes**, at `0x10000` in the 3 MiB
  factory partition; **291,456 bytes free**. The immutable
  1 MiB recovery partition is not an application install target.
- ESP-IDF 5.5.3, ESP32-C3, 8 MiB flash, no PSRAM. All seven `CITY_*_SMOKE` options
  were explicitly OFF for this image. The normal build/package identity is saved
  with the image; later documentation, host-test and USB-tool changes leave the device sources unchanged.
- Final active content: revision **12**, **16** baseline species. Previous valid
  content: revision **11**, the same baseline roster. Both synthetic packs were
  replaced. This does not claim 1,000 authored Pokémon have shipped.
- Final boot: `READY display=1 buttons=1 capture_count=20 place_data=1` and
  `BESTIARY_READY schema=12 count=20 sequence=21 migrated=0`; no diagnostic mode.

## Host and render evidence

`CMAKE_PREFIX_PATH=/private/tmp/city-pack-mbedtls-install ./tools/test-host.sh`
passed **43/43 targets**, Debug with address/undefined sanitizers on C tests.
This covers authentic P-256 verification, typed content, corruption/signature
rejection, every simulated partial upload position, torn activation/rollback
journals, I/O failures, leases, sparse-save migration and failure atomicity.

New runtime checks cover 10,000 catalog rows with a fixed 6,952-byte model,
package-only captures/duplicates/buddy/damage/visit/release, discovery-capacity
failure, missing-package rejection and output preservation. An authenticated
update changing a discovered package species' stats is rejected. Publisher PEM,
raw public key and firmware key bytes must match. The installation planner rejects
occupied extension space, bad table MD5/contracts and ambiguous damaged journals.
Audio tests cover actual package PCM reads, cancellation and read failures.

The production-derived LVGL harness passed geometry/glyph/overlap checks,
including both languages at cursors 0, 16, 252, 256, 996 and 1,000, package-only
detail and collection-capacity feedback. The host package-detail fixture has no
package artwork; real mapped sprite/audio handling is exercised on the device.
Selected late-page/detail renders were also visually inspected. Renders and logs
are private local build artifacts, not physical-display or subjective-audio signoff.

## Connected-device evidence

| Check | Observed result |
|---|---|
| Exact extension of the five legacy partitions | New A/B/selector regions are `0x420000/0x140000`, `0x560000/0x140000`, `0x6a0000/0x2000`; originally all erased |
| Legacy occupied unnamed bytes | Preserved, including data at `0x3fa000–0x419908`; the earlier proposed placement was rejected before writing |
| Trusted baseline install | Revision 7 activated, 16 species, original 20 copies loaded |
| Package-only catalog | Revision 8 activated, 17 species including ID 60001; cache, gameplay and sparse round-trip passed |
| Interrupted staging | Only first 65,536 bytes of revision 9 written to inactive A; device rejected it and retained B revision 8 |
| Incorrect signature | Full revision 9 with a flipped signature byte rejected; B revision 8 remained active |
| Large catalog activation | Revision 9 accepted: 1,000 synthetic entries, 1,279,980 bytes, 3,649 ms verification/activation |
| RAM across catalog sizes | Activation free heap **190,384 bytes** for both 17 and 1,000 entries |
| Actual cache eviction | Every catalog page and every sprite/cry asset block matched direct mapped data after bounded-cache reads |
| Sustained workload | **4,876 transitions**, **1,800,592 ms**; paged list, details, encounter/capture rendering and cries; no panic, read failure or leak assertion |
| Steady free heap | Start **136,572**, end **136,572** bytes; minimum observed **89,616** bytes, task stack watermark **6,980** bytes |
| Real NVS reboot | Two ID-60001 copies, individual stats/personality/HP, buddy selection and visit persisted through the production BSP adapter; reboot reload bytes matched; isolated test namespace cleaned |
| Automatic fallback | Damaged current A header; device booted authenticated B revision 8 and loaded all 20 real copies; A was then restored |
| Final publisher workflow | `install_content_usb.py` backed up, wrote revision 12/normal app, read back every byte, checked protected regions and required successful content/save boot |
| Final independent audit | Full 8 MiB readback matched the intended app and both packs; bootloader, PHY, identity, recovery and other protected/unnamed regions unchanged |
| Player preservation | All pre-existing logical NVS entries unchanged; player blob byte-for-byte identical: format 13, 20 copies, buddy species 25 / instance 4 |

The NVS diagnostic intentionally writes and erases **only** `city_pack_tst`; the
namespace was absent in the original backup. Therefore physical NVS bytes change
as expected, while every pre-existing logical value and the entire player's save
remain identical. No saved place identifiers or radio observations are published.

The interrupted upload and damaged-slot tests are controlled flash faults with
reboots. They do not claim a physical power-cut or brownout test.

The first publisher run passed full byte comparison but captured an empty boot
log. Its reset helper had left pyserial's default DTR asserted. The corrected
helper explicitly deasserts DTR, clears stale input, then resets USB Serial/JTAG.
A regression test checks the sequence; the second complete publisher run passed
backup, programming, full readback and boot verification. The first empty log and
successful recovery boot are retained alongside the retry evidence.

## Diagnostic provenance and private evidence

The 30-minute image used build ID `1b1d86710cd4-dirty`, source hash
`8fd1da45d26c551fab85f018b5ae88169eeb745537ee20b00c282586d2ba9b61`, image hash `10a908fac45f9755372fa4c95362bb5efafd4fa02d7608317630bfe2f9a938d6`.
Its domain, content loader, catalog, audio and BSP production sources match the
release. Subsequent application changes only added the separately gated NVS
reboot diagnostic; production release disables both diagnostics.

The NVS image used source hash `2eb412e96d557f776d04b35119af6c4886ab34f6c5c0d1c78df0fb2aca567ed0` and image hash
`3dfced4e5e610bad343fd6a03798d8adac4b43f43b0029b850825b034e5163fb`. Each image's identity is retained privately.
The final normal image has its own clean identity above and was independently
installed, fully read back and boot-checked after the diagnostics.

Private evidence is under `device-backups/stage3-completion/` (Git-ignored):
`before.bin`, installation/readback logs, `package-only-boot.log`,
`partial-boot.log`, `signature-boot.log`, `large-soak.log`, `nvs-reboot.log`,
`fallback-boot.log`, `final-install-retry/verified.json`, `final.bin`,
`final-normal-boot.log`, `final-verification.json` and per-image manifests.
The final full-backup digest is `f5e42ef7f5947377f969d5ba60ac65e99fe952c780b027b00fa981d1138111d0`.
Never publish these flash dumps or the private signing key.

## Remaining product boundaries

- Stage 2's explicitly deferred physical verification remains unfinished.
- A pack has a 1,310,720-byte slot limit; 10,000 is the host container ceiling,
  not a claim that 10,000 full-size sprites/cries fit this Passport. The baseline
  pack uses 1,021,188 bytes, leaving 289,532 bytes per slot; at the demonstrated
  additional species size of 46,158 bytes, six more full-size entries fit.
- Player storage supports 16 baseline plus 48 additional discovered species and
  160 owned copies. Releasing copies retains discovery history.
- The local signing key must be retained privately; public-key rotation requires
  firmware. Signing is integrity protection, not secure boot against physical
  attackers. New authored media still needs content/licensing approval.
- Next gate: demonstrate repeat-carry/re-exploration value, resume deferred
  acceptance, and make the content-release decision before considering Stage 4.
