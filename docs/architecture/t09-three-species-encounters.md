# T09 Three-Species Encounters

## Product scope

T09 adds Bulbasaur, Charmander, and Squirtle to the offline place loop.
This remains a controlled Pokemon prototype. Public or commercial builds must
replace names and artwork with original City Spirits content.

The feature deliberately does not add levels, evolution, combat, an individual
creature inventory, cloud content, or location semantics.

## Encounter contract

Each confirmed place has one dominant species and two secondary species:

| Place affinity | Dominant | Secondary | Rare |
| --- | ---: | ---: | ---: |
| Place 1, 4, 7... | Charmander 60% | Bulbasaur 25% | Squirtle 15% |
| Place 2, 5, 8... | Bulbasaur 60% | Squirtle 25% | Charmander 15% |
| Place 3, 6, 9... | Squirtle 60% | Charmander 25% | Bulbasaur 15% |

On the first confirmed encounter at a new place, already discovered species
receive zero weight while an unknown species remains. This guarantees a new
discovery without permanently binding one species to one physical place.

Selection is hardware-independent and deterministic for an injected
`place_id` and random seed.

## Individual attributes

Every encounter derives HP, Attack, and Defense from the species base values
plus an independently generated value from 0 through 15. The same place, seed,
and species always produce the same attributes.

The bestiary stores:

- lifetime capture count;
- last capture place;
- latest captured attributes;
- highest-total captured attributes.

It does not store every captured individual. This keeps the P0 collection loop
visible without introducing inventory management.

## Persistence

Bestiary schema v4 remains a fixed 156-byte checksummed blob. It contains three
fixed records and the existing monotonic settlement high-water mark.

Schemas v1, v2, and v3 migrate in place. Existing Charmander captures are
preserved and receive base attributes because old snapshots did not contain
individual values. Bulbasaur and Squirtle start as unknown.

An encounter or attribute result becomes visible only after the bestiary write
commits successfully.

## Validation

- Host tests cover deterministic selection, 60/25/15 distribution,
  new-place unknown prioritization, varying attributes, schema migration,
  transactional writes, and latest/best attribute updates.
- Firmware builds must stay within the 3 MiB factory partition.
- Device validation must migrate an existing schema-v3 snapshot, reboot, and
  preserve all three records.
- Field validation should sample at least 30 encounters per place across three
  places before changing production weights.
