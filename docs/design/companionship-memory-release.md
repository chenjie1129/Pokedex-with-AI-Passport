# Continuous companionship — owner trial

The owner authorized delivery of the three proposed increments in order on 2026-09-14. This extends the existing individual-personality release, including the companion scope otherwise gated by the original P0 roadmap. It does not authorize battles, more species, or generated conversation.

## 1. Remember the last adventure

Each individual retains its latest five committed shared events, oldest to newest in storage. Home recalls the newest event; **Our memories** on Home opens the selected buddy's history, newest first. UP/DOWN pages through events and OK returns. Each individual's actions also expose **Our memories**, even when that individual is not the selected buddy.

Events are first recorded outing, new place, eligible revisit, completed rest, and the two friendship-band milestones. They are facts represented by stable IDs with optional anonymous place numbers. No dates, network identifiers, raw locations, generated claims, or full strings are persisted. Old saves start with no memories; their real existing friendship and visited-place masks remain intact. Historical outings are not invented.

## 2. Recognize shared progress

The history page shows this individual's count of confirmed shared places. Crossing 20 or 60 friendship adds a milestone alongside the outing, in the same transaction. Existing personality-specific reactions accompany the discovery, with the milestone or saved-memory confirmation below them. At maximum friendship, a qualifying new visit still creates a memory and a reaction. Repeated scans at the same place do neither; revisits require an intervening different place as before.

The history is a bounded recent journal, not a permanent achievement archive. Older milestones can age out of its five-event limit; friendship and shared-place progress persist independently.

## 3. Something to do next

The history page suggests rest if injured, a first outing with no recorded shared places, a new place while the existing 16-place range is not exhausted, or a revisit afterwards. It names the existing action to open. For an unselected copy, exploration explicitly asks the player to set it as buddy first. Suggestions are read-only, offline, and have no daily obligations, timers, reward rules, or penalties.

## Save contract and recovery

Schema 12 retains the 48-byte header and 22-byte species records. Individual records grow from 20 to 32 wire bytes: count plus five two-byte facts, with one reserved zero byte. At 160 individuals the blob is 5,502 bytes, 1,920 bytes above schema 11. New fields remain within the existing atomic checksummed bestiary transaction; no extra write is made for Home, paging, or suggestions. The save worker stack is increased to 16 KiB for the larger model copy. Failed commits leave visible state unchanged.

Schemas 1–11 remain readable, including full-capacity schema-11 saves; newer unknown schemas and malformed event records fail closed. Schema 12 is not readable by older firmware. Use this build or a schema-12-compatible repair build; do not downgrade the app alone. Private pre-install backups are retained outside distribution artifacts.

Evolution remains the existing new-record flow and is not presented as a memory milestone. It does not yet preserve the source individual's relationship in the evolved copy. That continuity change is deferred explicitly rather than implied by this release.

## Verification and owner acceptance

Automated validation covers exact-copy isolation, failed-write rollback, duplicate events, reboot decode, full-capacity migration, history eviction, friendship thresholds, and malformed saves. Production navigation replay covers both entrances and return paths. LVGL renders cover English/Chinese memory types, empty states, final pages, invitations and returning Home. The opt-in personality diagnostic includes memory codec checks and production navigation without persisting its test creatures.

Owner trial: select a buddy, explore a confirmed new place, open its memories, reboot, and find the same event again. Switch copies to check their separate histories. Over three days note voluntary buddy/history opens, one memorable shared event, and whether it prompted another outing. These observations—not automated tests—decide whether continuity feels meaningful. Physical button feel, real-place acceptance, battery endurance and engagement remain separate from diagnostic evidence.
