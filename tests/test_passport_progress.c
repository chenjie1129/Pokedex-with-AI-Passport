#include "passport_progress.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool save_ok = true;
static bool persist(const city_bestiary_t *b, void *ctx) { (void)b; (void)ctx; return save_ok; }
static void catch_species(city_bestiary_t *b, uint16_t species)
{
    assert(city_bestiary_capture(b, b->last_settled_sequence + 1, species, 1, persist, NULL) == CITY_BESTIARY_APPLIED);
}
int main(void)
{
    city_place_catalog_t catalog = {.count = 2};
    catalog.places[0].place_id = 9;
    catalog.places[1].place_id = 2;
    city_passport_stamps_t stamps;
    assert(city_passport_stamps_from_catalog(&catalog, &stamps));
    assert(stamps.count == 2 && stamps.place_ids[0] == 2 && stamps.place_ids[1] == 9);
    city_passport_stamps_t before = stamps;
    catalog.places[1].place_id = 9;
    assert(!city_passport_stamps_from_catalog(&catalog, &stamps));
    assert(memcmp(&before, &stamps, sizeof(stamps)) == 0);
    catalog.places[1].place_id = CITY_WILD_PLACE_ID;
    assert(!city_passport_stamps_from_catalog(&catalog, &stamps));
    catalog.places[1].place_id = CITY_PLACE_INVALID_ID;
    assert(!city_passport_stamps_from_catalog(&catalog, &stamps));
    catalog.count = CITY_PLACE_MAX_COUNT + 1;
    assert(!city_passport_stamps_from_catalog(&catalog, &stamps));
    assert(!city_passport_stamps_from_catalog(NULL, &stamps));
    assert(!city_passport_stamps_from_catalog(&catalog, NULL));

    city_bestiary_t b; city_bestiary_init(&b);
    city_passport_progress_t p = city_passport_progress(&stamps, &b);
    assert(p.places == 2 && p.discovered == 0 && p.captured == 0 && p.goal == CITY_PASSPORT_FIRST_CAPTURE);
    assert(city_bestiary_mark_seen(&b, CITY_SPECIES_BULBASAUR, persist, NULL) == CITY_BESTIARY_APPLIED);
    p = city_passport_progress(&stamps, &b);
    assert(p.discovered == 1 && p.captured == 0);
    save_ok = false;
    assert(city_bestiary_capture(&b, 1, CITY_SPECIES_BULBASAUR, 1, persist, NULL) == CITY_BESTIARY_STORAGE_FAILED);
    assert(city_passport_progress(&stamps, &b).captured == 0); // No visible failed-write progress.
    save_ok = true;
    catch_species(&b, CITY_SPECIES_BULBASAUR);
    p = city_passport_progress(&stamps, &b);
    assert(p.goal == CITY_PASSPORT_CATCH_SPECIES && p.target == CITY_SPECIES_CHARMANDER && !p.target_seen);
    assert(city_bestiary_mark_seen(&b, CITY_SPECIES_CHARMANDER, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_passport_progress(&stamps, &b).target_seen);
    catch_species(&b, CITY_SPECIES_CHARMANDER);
    catch_species(&b, CITY_SPECIES_SQUIRTLE);
    catch_species(&b, CITY_SPECIES_SQUIRTLE);
    p = city_passport_progress(&stamps, &b);
    assert(p.discovered == 3 && p.captured == 3); // Species, not number of captures.
    assert(p.goal == CITY_PASSPORT_CATCH_SPECIES && p.target == CITY_SPECIES_PIKACHU);
    for (uint8_t i = 3; i < CITY_SPECIES_COUNT; ++i) catch_species(&b, city_species_id_at(i));
    p = city_passport_progress(&stamps, &b);
    assert(p.goal == CITY_PASSPORT_NEW_PLACE && p.target == 3);
    // Wild opportunity changes neither stamps nor place goal.
    city_wild_reward_guard_t guard; city_wild_reward_guard_init(&guard, NULL, 0);
    assert(city_bestiary_reserve_wild(&b, &guard, 0, CITY_SPECIES_SQUIRTLE, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_passport_progress(&stamps, &b).places == 2);
    assert(memcmp(&before, &stamps, sizeof(stamps)) == 0);
    // All capacities and page boundaries including the partially filled last page.
    for (unsigned count = 0; count <= CITY_PLACE_MAX_COUNT; ++count) {
        catalog.count = count;
        for (unsigned i = 0; i < count; ++i) catalog.places[i].place_id = i + 1;
        assert(city_passport_stamps_from_catalog(&catalog, &stamps));
        p = city_passport_progress(&stamps, &b);
        assert(p.pages == (count == 0 ? 1 : (count + 5) / 6));
        assert(city_passport_turn_page(0, p.pages, false) == p.pages - 1);
        assert(city_passport_turn_page(p.pages - 1, p.pages, true) == 0);
        if (count == 16) assert(p.goal == CITY_PASSPORT_COMPLETE);
        else {
            assert(p.goal == CITY_PASSPORT_NEW_PLACE && p.target > count && p.target <= 16);
        }
    }
    assert(city_passport_turn_page(8, 3, true) == 0);
    assert(city_passport_turn_page(0, 0, false) == 0);
    p = city_passport_progress(NULL, &b);
    assert(!p.places_ready && p.collection_ready && p.goal == CITY_PASSPORT_DATA_UNAVAILABLE);
    p = city_passport_progress(&stamps, NULL);
    assert(p.places_ready && !p.collection_ready && p.goal == CITY_PASSPORT_DATA_UNAVAILABLE);
    b.schema_version = 999;
    assert(!city_passport_progress(&stamps, &b).collection_ready);
    puts("Passport: stamps, persisted progress, goals, unavailable data and pagination passed");
    return 0;
}
