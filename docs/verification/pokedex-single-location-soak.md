# Single-location soak and storage-error acceptance

Decision (2026-09-16): the owner authorized deferring the two-location test for
this branch's save-expansion work. Cross-location discovery and the repeat-carry
product gate remain unverified. Physical power interruption remains unverified.

## Ready candidate

Clean committed source: `f7c7b26ebc47a111321e2d8ee47f168b64a75063`.
Local package: `../device-packages/pokedex-clean-f7c7b26/`.
The manifest reports `dirty=false`; firmware size is 2,838,464 bytes, with
307,264 bytes factory headroom. Its 33-test host baseline passes.

Installed and boot-verified on 2026-09-16 after a fresh validated 8 MB backup.
Full readback matches the clean app SHA-256
`5b39c7ddf232e0d1ca16ff32904f35377fa4c70a1aebb4b3e139b19dfe55c0f8`.
Every byte outside the app partition remained unchanged before first boot.
Boot reports `dirty=0`, schema 12, 20 owned records, catalog revision 6,
16 species, and display/buttons ready. Baseline buddy is instance 4 (Pikachu).

## Storage-error evidence completed on the host

The expanded production-handler replay covers discovery, capture, wild encounter,
buddy selection, evolution, healing and release. For each operation it checks:

- Failed persistence leaves the model and simulated stored state unchanged.
- The actual worker publishes the storage-error state and retains the operation.
- A second tap during an in-flight write does not dispatch a second task.
- OK retries and reaches the operation's correct success screen after success.
- Up returns Home without another write or losing the existing collection.
- Task-creation failure remains retryable.

Both English and Simplified Chinese error screens are rendered from production
UI functions. Standard render suite: 594 fixtures passed bounds, glyph, clipping
and overlap checks; representative capture/release failure frames were visually
inspected. The count excludes optional private device-save fixtures.

The working-tree host suite has 34 passing tests, including conservative soak
log evaluation. These are host/desktop results. An on-device storage-error screen
and its physical buttons have not been tested in this round.

## Operator run (two actual hours)

Keep the device in one location. Keep USB and battery power stable. Do not
simulate this test by unplugging power. Start with enough collection entries to
browse and audible sound settings; record the initial collection/buddy/settings
and a private NVS backup. Keep a small written action log, especially captures,
releases, buddy/setting changes and intended resets.

From the repo root in an environment with pyserial and esptool installed:

```sh
python tools/observe_pokedex_soak.py --port /dev/cu.usbmodemYOUR_DEVICE \
  --expected-commit f7c7b26ebc47a111321e2d8ee47f168b64a75063 \
  --output-dir /absolute/private/path/soak-20260916 \
  --enable-reset-requests --reset-on-start
```

Use a NEW output directory. `--reset-on-start` performs the initial reset after
opening the logs, capturing build identity and the first READY marker. Without
that option, if the game is already running,
go Home, wait five seconds, then request the initial observed boot with:

```sh
touch /absolute/private/path/soak-20260916/reset.request
```

The request is honored only when the most recent state is Home and it has been
idle at least five seconds. The observer does not flash or write saves. Resets
are operator-requested USB hard resets, not power cuts. If the USB port drops and
observation stops, the run is INCOMPLETE; reconnect and inspect rather than
claiming an uninterrupted pass.

| Elapsed | Actions |
|---|---|
| 0–10 min | Record initial state, warm up with normal browsing and scans |
| 10–110 min | Repeat browse → detail → cry → scan → catch/return → settings → Home |
| Around 20/40/60/80/100 min | Return Home, wait five seconds, create reset.request; verify state survives |
| 110–120 min | Make at least two final scans; inspect collection, buddy, settings and responsiveness |
| After recording | Final reboot and private NVS readback; compare against action log and initial save |

Minimum totals:

- 20 scans with result telemetry;
- 50 list/detail transitions;
- 20 completed cries (starts/cancellations alone do not count);
- 10 confirmed persistence operations (duplicate rewards do not count);
- 5 deliberate hard resets after the initial observed boot;
- full two hours, no panic/watchdog/unexpected reboot or save loss;
- minimum heap at least 32,768 bytes;
- comparable post-warm-up heap spread at most 8,192 bytes, final heap sample
  within the last ten minutes, and no long observation gaps.

Do not mute the device or interrupt every cry if those 20 completed-playback
samples are still needed. Leave at least ten minutes after a reset before the
warm heap samples for that boot; take two scans in that period.

`summary.json` can report INCOMPLETE or READY_FOR_MANUAL_REVIEW. It never grants
full release approval. The owner must confirm resets were intentional, button
and audio behavior stayed normal, and the before/after save audit matches the
recorded operations. Raw logs and NVS/flash backups remain private.

## Completed hardware observation (2026-09-17)

Recorded 7,200.1 seconds from 2026-09-16 23:56:02 to approximately
2026-09-17 01:56:02 China time. **Idle/reset stability observation completed;
interactive soak INCOMPLETE.** Only Home was observed, with zero scans,
list/detail transitions, cries or confirmed save operations.

Verified evidence:

- Clean `f7c7b26` identity on all six READY boots: initial boot plus five
  requested USB hard resets at elapsed 23.4, 44.7, 66.2, 87.7 and 109.2 minutes.
  These occurred at heartbeat checks after their target checkpoints, while
  Home was idle. No additional application boots were observed.
- No detected panic, watchdog, abort or storage error. Maximum serial log gap
  was 30.2 seconds; 241 battery samples were recorded.
- Each boot loaded schema 12, 20 owned records, sequence 21 and catalog revision
  6 with 16 species. Boot heap minimum was 184,980 bytes. No scan/workload heap
  samples exist, so steady-state drift and final workload heap remain unverified.
- After the recorder exited, a fresh 8 MB readback matched the post-install
  flash byte-for-byte. NVS (including settings), identity, PHY and recovery
  matched the original backup. Decoded bestiary bytes and unique instance IDs
  were unchanged; Pikachu instance 4 remained buddy.
- A final normal reboot after readback again loaded all 20 records and reported
  clean build identity plus display/buttons ready. This boot is outside the
  two-hour observation.

Private backups, logs, reset ledger, recorder summary and preservation audit
remain under `/private/tmp/pokedex-soak-20260916/`. The follow-up heartbeat is
paused after completion. No merge was performed during the soak. The owner subsequently authorized main publication on 2026-09-17.

Remaining acceptance: an actual interactive two-hour run with the action counts
above and comparable warm/final heap samples; physical button/audio acceptance;
storage-error screen/button acceptance. Location testing remains deferred and
physical power-cut testing remains unverified. These checks are deferred and remain unfinished under the owner decision in
[pokedex-stage2-deferral-2026-09-17.md](pokedex-stage2-deferral-2026-09-17.md).
Stage 3 development may proceed; this is not an acceptance pass.
