# Buddy Pokemon and Poké Ball correction

Choose a captured species in Bestiary and press UP on its detail page. The selection is saved by the existing worker before Home shows the buddy. Seen-only species cannot be chosen. OK still returns to the list.

Home shows the buddy artwork and Bond /100. Every successfully persisted capture with a buddy earns one point, including eligible Wild captures. The first capture at each of the 16 saved places with that buddy earns two extra points. Failed/abandoned captures and repeated encounter sequences earn nothing. The place bonus is per species, remembered permanently, and cannot be renewed by switching buddies or rebooting. At 100 the label becomes BEST BUDDY. These points do not yet grant evolution or capture advantages.

Friendship is per species. Switching buddies preserves both species' progress. Existing saves start without a buddy and without retroactive points. Collection counts and stats remain intact.

Schema 7 uses the existing 276-byte envelope: buddy species ID at header bytes 24–25, friendship at each record's bytes 16–17, visited-place bitset at bytes 18–19. Schemas 1–6 decode with zero buddy fields. The existing NVS key bestiary_v6 remains the storage address; its payload schema determines decoding. On load an older payload is rewritten and read back before publication. Corruption never triggers fallback to an older collection. Back up NVS before install: older firmware cannot read schema 7 in this key.

Capture settlement and friendship share the same checksummed blob and sequence high-water mark. A failed persistence call leaves the live model unchanged. Selection is also a domain transaction, with no LVGL-thread flash writes.

The Poké Ball red hemisphere uses a circular child inside a rectangular half-height viewport. This avoids rounded clipping layers on the ESP32-C3; its inner dimensions account for the outline, which is drawn last. Launch position and the initial throw trajectory now fit the complete 60-pixel ball inside the field. The capture animation continues to use the existing timing and outcome logic.

Validation covers domain save failure, duplicate sequences, switching, Wild, place 1/16, maximum friendship, schema 6 migration, invalid checksummed fields, and adapter migration commit failure. Production LVGL render cases cover all 12 buddy names at 0/50/100, detail pages, no-buddy Home, and launch/catching ball screens. Physical interaction is a separate device acceptance step.

Capture renderer regression checks use the same 240 x 20 partial buffer as the device. A dedicated CITY_CAPTURE_RENDER_SMOKE build pauses gameplay timers and skips button registration, renders three throw/catching cycles without rewards, and reports RENDER_SMOKE_PASS. Always build with the option OFF before normal installation.
