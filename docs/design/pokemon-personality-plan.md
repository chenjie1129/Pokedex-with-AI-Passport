# Individual Pokémon personality and growth integration

Status: detailed proposal, 2026-09-14. Source baseline: `dbe9027`.
This task delivers a plan only. It does not authorize or claim firmware implementation, flashing, deployment, or a product-gate pass.

## Outcome and product boundary

Make a particular captured Pokémon recognizable through a stable personality, authored reactions, and a relationship developed through exploration. Answer: does an individual companion make people want to carry the device and explore again?

Deliver in this order: personality labels and dialogue → friendship reactions → small growth bonuses → temporary moods. Each stage must work on its own and have a stop/go review. Repository policy places companion growth after the P0 continuation gate; drafting this plan does not establish that the gate has passed. Implementation begins only when that gate is documented or the owner explicitly changes the boundary.

The feature works entirely offline, without a phone, account, GPS, cloud service, or LLM. It uses saved, confirmed game events rather than raw scans. No raw SSIDs, BSSIDs, public BLE addresses, precise locations, or wall-clock assumptions enter its state or evidence.

### Included

- One permanent personality per owned instance, including independently captured copies of the same species.
- Specific-instance buddy selection and correct targeting of companion actions.
- Short English and Chinese authored text using existing sprites and UI primitives.
- Individual friendship for new shared experiences, separate from legacy species bond.
- Small additive bonuses to an independently implemented XP/friendship system.
- Three derived moods that change presentation only.
- Versioned save migration, failure-safe settlement, bounded embedded-resource use, host/render/device verification.

### Excluded

- Official Pokémon Nature mechanics or claims of canonical personality behavior.
- Personality-driven penalties, stat multipliers, rare personalities, personality rerolling, breeding, trading, or personality items.
- New battle systems, new species, evolution redesign, skill loadouts, equipment, hunger, sickness, death, or time-away punishment.
- Voice recording, speech recognition, generated conversation, TTS, cloud memory, or notifications.
- A new animation engine, new sprite sets, or changes to existing cry assets, volume, brightness, and mute behavior.
- Anti-cheat guarantees against save editing or clock manipulation.

## Verified starting point and ownership boundaries

`components/city_domain/include/bestiary_service.h` currently defines schema 10, capacity for 160 owned Pokémon, and 16 wire bytes per owned entry. Each individual has an ID, species, capture place, HP/Attack/Defense, current HP, and evolved/migrated flags. Level, XP, personality, and individual friendship are absent.

Buddy selection and friendship are species-level. Some damage/recovery paths select an individual by matching the species' best stats. Evolution currently creates another owned individual. These are source findings, not claims about newly tested firmware.

| Area | Owner and rule |
| --- | --- |
| Identity, personality, friendship, XP eligibility | Hardware-independent domain code; UI never edits save models |
| Capture randomness | Injected source; select personality once, then persist with capture |
| Persistence | Versioned/checksummed encoder and existing durable-save path |
| Text and translation | Firmware-owned content catalog keyed by stable personality/event/bond IDs |
| UI | Renders committed state; optional feedback never blocks capture or exploration |
| Hardware | BSP adapters; no flash I/O or scanning on the LVGL task |
| Online conversation proposal | Separate workstream; may later read committed personality but cannot change it or award rewards |

Do not silently substitute this individual personality for the species characterization in `buddy-conversation-plan.md`. That draft currently uses species identity. An explicit future integration must select the same individual and pass only allowlisted committed state.

## Shared foundation: exact individual identity

Before showing buddy reactions, add `buddy_instance_id`; derive species from that record. All affected buddy HP, recovery, friendship, and reward paths must resolve by instance ID, never by identical stats. Selecting another buddy neither heals nor earns rewards. Releasing the selected buddy clears selection; it does not silently choose another copy.

For an old species-only buddy, preserve its old species selection as migration context and ask the player to choose a copy when next entering the buddy flow. Do not invent which copy was previously selected. Collection and exploration remain usable during this one-time selection.

Preserve legacy species bond and evolution eligibility through a clearly separated compatibility path. New individual friendship must not overwrite old bond or retroactively distribute it among copies. Existing evolution behavior remains outside this plan: a newly created evolved record receives a one-time assigned personality, without claiming continuity from an unidentified source copy. A later in-place evolution feature must preserve personality and identity and requires its own scope decision.

Foundation acceptance: two identical-stat copies can be selected, damaged, recovered, and released independently; selected identity survives reboot; selection failure leaves the old state intact; old species bond/evolution regressions are absent.

## Stage 1 — Personality labels and authored dialogue

Question: can the owner recognize a specific companion before mechanical bonuses exist?

Assign uniformly from Curious, Brave, Calm, Playful, Affectionate, and Independent. All six are equally available and have no rarity or power ranking. Brave can ship as a label now even while its future battle bonus is unavailable.

| Personality | Voice direction | Example discovery line |
| --- | --- | --- |
| Curious | Notices unfamiliar things | “What is over there?” |
| Brave | Encouraging, willing to try | “Let's explore ahead!” |
| Calm | Observant, unhurried | “Let's take a look.” |
| Playful | Light, energetic | “A new place to play!” |
| Affectionate | Enjoys sharing experiences | “I'm glad we came together.” |
| Independent | Confident, comfortable alongside you | “I'll look ahead.” |

Content budget: six personalities × four contexts × two variants = 48 lines per language. Contexts are buddy detail, confirmed new place, eligible revisit, and completed recovery. Use a deterministic bounded selector rather than an LLM; text selection never changes rewards. Avoid guilt, possessiveness, unverified event claims, and commands that imply an unavailable game action.

Detail screen shows personality near the name/level area; level appears only if implemented. Reactions occupy a small dismissible region of an existing result screen. No unsolicited cry playback. During overlapping results, saved capture/reward/HP information takes priority over personality text. At most one personality reaction per settled event.

Persist personality with each new capture. Migration assigns existing instances a stable value once using a documented versioned mapping of instance ID; this is newly introduced characterization, not recovered history. Preserve all original stats, current HP, and legacy labels. Pin numeric personality IDs so text reordering cannot change saved meaning.

Acceptance: every owned instance has a valid stable personality after successful migration; repeated loading never rerolls it; duplicate/retried captures never produce another assignment; save failure publishes neither capture nor personality; all 96 localized lines fit the 240×320 display and supported fonts.

Stop/go: compare two companions with different personalities during a short owner trial. Continue if their reactions are understandable and distinguishable without getting in the way. Revise wording before adding mechanics if labels alone do all the work.

## Stage 2 — Friendship reactions

Question: does the relationship feel different after shared exploration?

Add individual friendship in the range 0–100. Existing instances start at 0 for this new metric; historical species bond remains separately preserved. UI identifies it as the new individual friendship system during migration onboarding and must not present the old bond as lost.

Initial tunable rules: a first confirmed place visited together grants +5; an eligible revisit grants +2. First-together and revisit are mutually exclusive for one event. Track an individual visited-place mask using the existing bounded place-ID domain; do not add location tracking. Opening detail, selecting buddies, recovery, captures alone, and repeated scans grant no friendship. At cap, show no positive gain unless points actually increased.

Use three bands: Getting acquainted (0–19), Familiar (20–59), Close (60–100). Friendship changes dialogue warmth only in this stage. No combat benefit, evolution requirement, reward unlock, or gated basic interaction.

Reuse Stage 1 event lines. Add six personalities × three bands × two buddy-detail lines = 36 lines per language, replacing the 12 original detail lines. Result: 72 lines per language, avoiding a full personality × event × mood × friendship matrix.

Acceptance: only the buddy assigned to the settled event gains friendship; duplicate/reboot retry grants nothing; changing buddy while a save is pending cannot redirect rewards; band boundaries at 19/20 and 59/60 behave correctly; integer clamping and storage failures are covered; another copy's friendship remains unchanged.

Stop/go: owner observes one companion across several outings and checks whether warmer reactions are noticed without explaining the thresholds. Use a manual trial diary for cross-day observations; device boot counts are not dates.

## Stage 3 — Small growth bonuses

Question: do modest preferences make raising different companions enjoyable without making some personalities undesirable?

Hard dependency: base level/XP/stat growth must already exist and pass its own tests. The earlier growth proposal is input, not delivered code. Its required contract is per-instance XP, deterministic thresholds and stat derivation, surplus XP, level cap, safe arithmetic, persistent current HP, and transactional idempotent reward settlement. Full battle implementation remains separate.

| Personality | Proposed additive bonus | Availability requirement |
| --- | --- | --- |
| Curious | +10% new-place XP | Authoritative new-place reward |
| Brave | +10% battle XP | Implemented, settled battle result |
| Calm | +10% eligible-revisit XP | Persistent revisit eligibility |
| Playful | +10% capture-event XP | Settled capture attributed to buddy |
| Affectionate | +10% individual friendship | Stage 2 settlement |
| Independent | +1 XP per eligible place visit | Settled place event |

Do not advertise inactive bonuses. Ship Stage 3 only when all six have useful supported effects, or obtain a recorded design decision replacing Brave's bonus. No battle work is implicitly authorized by this dependency.

For 10% bonuses, accumulate fractional tenths per individual: `sum = remainder + base_reward`, `bonus = sum / 10`, `remainder = sum % 10`. Save the remainder in the same transaction. This avoids rounding all +2/+5 friendship bonuses to zero and avoids overpaying every tiny reward. The percentage never applies recursively to other bonuses. Independent grants a fixed integer and requires no remainder.

Bind the recipient ID and eligible event type before settlement. Compute base reward, personality bonus, cap-adjusted actual gain, level changes, HP effect, and deduplication marker in one next-state transaction. Publish only after durable success. Replay must not grant either the base or bonus twice. Do not add an unbounded event log; extend the authoritative ordered reward ledger or an explicitly bounded receipt design after inspecting every producer.

Personality does not change raw HP/Attack/Defense formulas, incoming damage, recovery, or evolution eligibility. Faster leveling can indirectly improve stats through the base growth system. Friendship percentage changes must not alter the separate legacy evolution bond path.

Acceptance: 10 eligible +2 rewards yield exactly +2 cumulative percentage bonus; reboots preserve fractional progress; duplicate events preserve XP/remainder; full-cap events neither accumulate future bonus credit nor claim unavailable gains; multi-level and stat arithmetic cannot wrap; failed saves leave the entire reward unchanged.

Stop/go: simulate equal event traces across personalities, including new-place-heavy, revisit-heavy, and capture-heavy use. Report actual differences without combining friendship and XP into a fictional common score. Conduct a trial to identify universally preferred bonuses before freezing values.

## Stage 4 — Temporary moods

Question: does a small visible reaction make the companion feel responsive?

Only three moods: Content, Excited, Tired. Mood has no effect on rewards, stats, eligibility, friendship, or recovery cost. It is not a permanent personality change.

- Tired: current HP is at or below 25% of maximum, including zero. Use safe integer comparison. Existing zero-HP gameplay rules still apply.
- Excited: a successfully saved new-place discovery by this buddy; lasts only for that result/detail interaction, ending on dismissal, departure from the flow, buddy switch, or reboot.
- Content: all other cases. A healed Pokémon recomputes mood from its resulting HP.
- Priority: Tired overrides Excited; zero maximum HP/invalid records use a safe unavailable state instead of division.

Derive low-HP mood from committed HP. Keep the excitement hint in RAM; no timer, trusted date, extra flash write, or persistent mood field. Reboot clears excitement but still shows Tired if HP is low. Reuse existing visual primitives with a localized mood word; do not require new art. Optional animation must respect existing input responsiveness and rendering budgets.

Acceptance: correct behavior at zero HP, 25%, just above 25%, full HP, recovery, failed recovery, buddy switch, and reboot. Mood never hides HP or changes damage. No background care loop or write amplification.

## Data, migration, and embedded budget

Persistent additions across stages: personality ID, buddy instance ID, individual friendship, individual visited-place mask, and one fractional bonus remainder where required. XP and growth baselines belong to the separate growth model. Derive friendship bands and mood; do not store display strings per Pokémon.

Illustrative maximum for personality integration is 1 + 1 + 2 + 1 = 5 wire bytes per individual, or 800 bytes at 160 individuals, plus the buddy ID/header/version overhead. This assumes the existing place set fits 16 bits and excludes XP fields, receipts, NVS metadata, alignment in RAM, migration copies, and garbage collection. It is a planning estimate, not capacity proof.

Inspect actual free NVS and maximum populated saves before choosing a schema number. Use explicit serialization, never raw C struct persistence. Test migration at full collection capacity; preserve individual stats/HP and unknown-history labels; validate ranges and checksums; reject unsupported future schema safely. Power interruption must leave either the valid old save or valid complete new save. Do not silently fall back to an old writable save after a successful migration. Define the recovery/downgrade route before flashing.

Keep text in flash and construct only visible UI. Measure fonts, firmware size, worst-case heap/largest block, save latency, and flash-write count on target. No partition changes, full-flash writes, device-identity changes, or storage expansion are included. If migration cannot fit, stop that stage and revise the format rather than assuming unused flash is available.

## Verification and completion evidence

For every implementation stage, run `./tools/test-host.sh` and meaningful added domain tests. Render representative English/Chinese screens with all personalities, friendship bands, HP boundaries, save errors, and long translated strings. Confirm glyph coverage, clipping, button labels, and dismiss/back behavior.

Then report firmware build and physical device tests separately: install via the project's protected app-only path; capture/select two same-species copies; trigger valid and duplicate rewards; recover; switch buddies; restart; verify stable identities and values. Test interrupted storage through host fault injection first and the supported device diagnostic path where available. Do not describe host tests as physical validation.

Deliver each stage with changed paths, schema compatibility, automated results, rendered evidence, actual-device observations, remaining limitations, and the product stop/go decision. No performance or retention claim without measured evidence. No automatic commit, push, release, or online conversation integration is part of this planning task.

## Work packages and release boundaries

| Package | Depends on | Concrete output | Release boundary |
| --- | --- | --- | --- |
| A. Identity and migration design | Documented product gate | Instance buddy/API contract, compatibility rules, save-size proof | Internal foundation; no personality launch yet |
| B. Labels and dialogue | A | Stage 1 domain/content/UI plus migration | First useful personality release |
| C. Friendship reactions | B and owner review | Individual points/place history and reaction bands | No XP or combat changes |
| D. Growth integration | C plus separately verified XP system and six useful effects | Transactional additive bonuses and reward breakdown | No new battles, evolution, or raw-stat personality modifiers |
| E. Temporary moods | D and owner review | Derived HP mood and transient discovery excitement | Presentation only |

If D is blocked by the XP/battle dependency, B and C remain complete usable releases. E stays queued under the agreed sequence unless the owner explicitly reprioritizes it. Tune numbers and wording within the above contracts; adding another system, changing evolution semantics, or requiring connectivity is a new scope decision.

## Implementation decision — 2026-09-14

The owner instructed implementation and verification before handoff. This explicitly authorizes the first usable personality release despite the earlier planning-only/P0 boundary. Packages A–C are bundled for the owner's first trial: exact-copy identity, personality labels/dialogue, and individual friendship. The trial still needs to establish whether personalities and warmer reactions are noticeable. Packages D and E remain queued under the agreed dependency order; no XP, battle, raw-stat growth, or mood feature is implied by this release.

Implementation uses schema 11: a 48-byte header and 20-byte owned records, totaling 3,582 wire bytes at the existing fixed 160-copy capacity (648 bytes more than schema 10). The additional per-copy last friendship place makes revisit eligibility persistent: this individual must visit another confirmed place between repeat rewards. Repeated scans at the same place do not award friendship or write another identical record. Captures continue to grant the separately labeled legacy species bond, while individual friendship is settled when the confirmed place encounter is offered, regardless of capture success.

Existing species-only buddies require explicit copy selection; IDs, stats, HP, legacy flags, and old evolution bond are preserved. Old firmware cannot read schema 11. The recovery route is to retain this firmware or a schema-11-compatible repair build; downgrading the application alone is unsupported. The local pre-install backup is an emergency recovery artifact and is never part of the distributable firmware.

A compile-time, default-off `CITY_PERSONALITY_SMOKE` diagnostic exercises temporary in-memory creatures and production navigation. It restores the loaded collection in RAM and never writes its test creatures to player storage. Normal boot migration is separately verified by NVS readback. The normal handoff image has all diagnostic modes disabled.
