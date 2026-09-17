# Stage 2 verification deferral and Stage 3 authorization

Decision date: 2026-09-17. Accountable owner: @chenjie1129.

The owner explicitly requested: defer Stage 2 device tests, mark verification
unfinished, commit and push to main, then begin Stage 3. This supersedes the
earlier no-merge instruction and the Stage 2-to-3 serial gate for development.
It does not mark Stage 2 completed or constitute full device/release acceptance.
Stages 4–6 retain their product and capacity gates.

## Evidence retained

- Clean `f7c7b26` firmware: 2,838,464 bytes; 307,264 bytes app headroom,
  leaving 45,120 bytes above the existing 256 KiB reserve target.
- Host migration/adapter checks, production-handler storage-error replay, and
  English/Chinese desktop render evidence.
- Real NVS fault diagnostic evidence, including storage exhaustion and software
  restart after commit. Software restart is not a physical power interruption.
- Two-hour idle/reset observation: five planned resets, no detected runtime
  faults, unchanged flash/save readback, and successful final normal boot.

See [soak report](pokedex-single-location-soak.md) and
[NVS fault report](pokedex-nvs-fault-2026-09-15.md) for evidence and limits.

## Unfinished verification — deferred, not passed

| Check | Remaining evidence |
| --- | --- |
| Interactive two-hour soak | Actual scan/browse/cry/save workload, warm heap drift and final workload heap |
| Physical storage-error interaction | On-device Retry/Back behavior and recovery from failed saving |
| Physical power interruption | Previously committed saves remain valid after an actual interrupted write |
| Physical controls/audio/carry acceptance | Responsive buttons/pages, subjective audio and defined carrying/battery workload |

Two-location field testing is separately deferred; repeat-carry value remains
unproven. No missing result is converted into a pass by merging to main.

## Stage 3 scope now authorized

Start with the offline host content-pack format, signatures, object integrity
and corruption tests. Continue toward bounded device providers, storage,
activation/rollback and cache only with corresponding evidence. Preserve NVS,
PHY, identity and recovery contracts. A larger logical test catalog is not a
claim that all its assets fit on this device or are playable in the current game.

Resume the deferred checks before claiming full device acceptance. Any new
firmware still needs its own build and relevant verification; the exception
does not allow relabeling host evidence as hardware evidence.
