# Persistent HP and release

Schema 9 added one-byte current HP to every species record. Schema 10 adds a bounded collection of 160 individually addressable owned Pokemon. Each entry has a stable ID, species, capture place, stats, current HP, capture/evolution origin, and a marker for entries reconstructed from an older aggregate save. Schemas 1–9 remain readable and are rewritten through the existing verified NVS migration path.

New captures create distinct owned entries at full HP. The species summary remains available for the Bestiary and buddy features, while the individual list provides identity for release and future battle work. Damage floors at zero, and recovery restores the selected representative's maximum. Operations copy, persist, and only then publish the new model, so a failed write leaves the visible state unchanged.

The detail screen exposes an action menu with recovery, evolution, release and back. Release opens an individual picker showing copy number, stable ID, HP, Attack, Defense, and capture origin/place. The following confirmation is bound to that exact stable ID and defaults to KEEP. Each successful release removes only the selected Pokemon and recomputes the species summary from the remaining entries. Releasing the final owned Pokemon returns the entry to SEEN, clears its saved progress and clears buddy selection if that species was active.

Older saves contain counts and only latest/best representative stats, so migration cannot recover attributes that were never stored. It creates stable `LEGACY COPY` entries, preserving every count; the known latest and best stats are retained and the remaining legacy copies use the best known stats. Every new capture after migration records its actual individual attributes.

Host coverage includes individual IDs and selection, damage boundaries, recovery, save failure rollback, schema migration, encode/decode persistence, selected-copy release, summary promotion, unrelated-species preservation, final-release cleanup, buddy clearing, evolution-only release and recapture. Production render coverage includes the individual picker, HP, actions, both confirmation choices and the released state. Physical button and NVS testing remain separate device acceptance steps.

## Health status and individual browsing

The action screen shows the strongest owned representative's health and a separate status (Health is full / Needs healing). Heal retains its action label, is muted at full health, and is skipped by navigation. Full-health activation cannot queue a write. My Pokemon opens a read-only copy browser; Up/Down changes the owned ordinal and OK returns to the action menu. This browser displays the selected entry's HP, Attack, and Defense, rather than the aggregate best values. Legacy entries visibly explain that their stats may be shared. No historical stats are rerolled.

When a capture becomes the strongest representative, the summary now takes that new individual's current HP along with its stats. Damage to the previous individual remains on that individual, and persistence failure leaves both unchanged. Schema and existing saved values are unchanged by installation.
