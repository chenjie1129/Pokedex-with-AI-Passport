# Catalog and save compatibility

Stage 2 candidate: catalog revision 6, 16 entries, save schema 12.

| Input | Behavior |
|---|---|
| Schema 12, catalog v5 / 15 entries | Map records by stable ID; append UNKNOWN 60000; commit and read back before publication |
| Schema 12, catalog v6 / 16 entries | Load without a migration write |
| Schema 10 or 11, supported smaller catalog | Existing schema conversion plus stable-ID catalog expansion |
| Legacy schemas 4–9 | Existing legacy decoder constraints remain; not a promise of arbitrary catalog size |
| Future schema/count, unknown ID, duplicate ID, invalid CRC | Reject; retain current UI state and never fall back to stale legacy data |
| Migration set/commit/read-back failure | Return failure without publishing the candidate model; retry on next load |

Catalog revision is serialized at header offset 20; migration updates it from 5
to 6. Wire schema, record count and stable IDs determine decoder compatibility. ID 60000 is reserved for
Mossbit; never reuse it for a different creature. National IDs remain below 60000.

`city_catalog_provider_t` exposes immutable definitions and bounded iteration.
The compiled provider remains limited by CITY_SPECIES_COUNT (16) and generator
limit (32). This is not a dynamic package loader or unbounded runtime catalog.

A 15-entry firmware rejects the upgraded 16-entry blob. Downgrade requires a
separate, explicit restore decision using this device's own pre-upgrade backup;
never silently truncate or discard records.

Host failure injection verifies adapter publication and retry behavior. It does
not establish real NVS-full behavior or power-cut atomicity; Round C and physical
acceptance remain separate evidence gates.
