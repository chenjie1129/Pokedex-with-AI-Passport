# Encounter eligibility and build identity

## Current scan evidence controls encounters

A remembered place and permission to start an encounter are separate outputs.
During the five-minute stability lock, an empty, sparse or gray scan may retain
the prior place in memory, but it cannot authorize discovery or capture.

| Current result | Device route | Encounter eligible |
| --- | --- | --- |
| Fresh known-place match, including during lock | Known place | Yes |
| Gray evidence during or outside lock | Gray status | No |
| Empty or fewer than four usable APs | Wild status | No place encounter |
| Scan error or invalid input | Error | No |
| New-place candidate, awaiting confirmation | Waiting | No |
| Confirmed candidate, storage failed | Storage error | No |
| Confirmed candidate, storage succeeded | New place | Yes |

`city_place_scan_decide()` is the shared production and Host-test mapping.
The coordinator publishes its eligibility decision with the scan result; the
UI checks that flag before selecting a species or starting a discovery write.
Strong new-place evidence can still begin confirmation during the stability
lock. Weak evidence does not extend that lock. The Wild gameplay and reward
cooldown integration remain the separate T10 task.

## Build identity

CMake generates an application version of the form:

```text
<12-character-commit>-<10-character-source-hash>[-dirty]
```

The full Git commit and SHA-256 of the non-ignored source snapshot are in
`build/firmware/identity/build-identity.json` and in the `BUILD_ID` boot log.
The fingerprint includes tracked content, deletions, executable modes, symlink
targets and non-ignored untracked files. Ignored build outputs do not affect it.
`dirty` means the checkout differs from the commit, including untracked files.
It must not be interpreted as a released commit.

Incremental builds rerun identity generation after source or Git-state changes.
Build from a Git checkout. Generated artifacts and package output belong in an
ignored build directory or outside the repository.

After activating ESP-IDF 5.5.3:

```sh
./tools/test-host.sh
idf.py -B build/firmware -D IDF_TARGET=esp32c3 build
python3 tools/package_firmware.py --build-dir build/firmware \
  --output-dir ../device-packages/<version>
```

For a development device test from uncommitted changes, explicitly add
`--allow-dirty`. Packaging rejects source changes since configuration and a
binary whose embedded version differs from the generated identity. Ordinary
release packaging rejects dirty source.

The package contains the application image, source identity, full image hash,
ELF hash, ESP-IDF version, build configuration and its hash, installation guide,
and SHA256SUMS. The source fingerprint identifies application source; the
image and configuration hashes identify the built artifact and configuration.
It is not a signature or proof of a reproducible toolchain.

## Verification before device handoff

Host checks exercise the actual scan-result policy for known -> gray, empty,
sparse and scan failure, then fresh matching evidence. They also cover denied
candidates and storage failure followed by successful place persistence.
Version checks create isolated Git fixtures, modify tracked and untracked
files, rebuild without manual reconfiguration, and reject stale image packages.

On the device, check the `BUILD_ID` line and retained bestiary count after boot.
Explore a known place and complete a capture. With insufficient evidence, the
device should show a status rather than an encounter. Cross-building movement,
radio conditions and physical button feel still require a real-device test;
Host tests alone cannot establish them.
