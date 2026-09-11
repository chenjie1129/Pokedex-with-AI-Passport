#include "bestiary_service.h"

#include <assert.h>
#include <string.h>

static bool save_ok = true;
static unsigned writes;

static bool persist(const city_bestiary_t *next, void *context)
{
    (void)context;
    ++writes;
    assert(city_bestiary_is_valid(next));
    return save_ok;
}

int main(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    city_creature_stats_t high_hp = {50, 55, 40};
    assert(city_bestiary_capture_with_stats(&bestiary, 1, 25, 1,
        &high_hp, persist, NULL) == CITY_BESTIARY_APPLIED);
    city_creature_record_t *pikachu = city_bestiary_record(&bestiary, 25);
    assert(pikachu->current_hp == pikachu->best_stats.hp);

    assert(city_bestiary_apply_damage(&bestiary, 25, 7, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(pikachu->current_hp == (uint8_t)(pikachu->best_stats.hp - 7));
    uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
    assert(city_bestiary_encode(&bestiary, encoded));
    city_bestiary_t restored;
    assert(city_bestiary_decode(encoded, sizeof(encoded), &restored));
    assert(city_bestiary_record_const(&restored, 25)->current_hp == pikachu->current_hp);

    city_bestiary_t before = restored;
    save_ok = false;
    assert(city_bestiary_recover(&restored, 25, persist, NULL) == CITY_BESTIARY_STORAGE_FAILED);
    assert(memcmp(&restored, &before, sizeof(restored)) == 0);
    save_ok = true;
    assert(city_bestiary_recover(&restored, 25, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_record_const(&restored, 25)->current_hp ==
           city_bestiary_record_const(&restored, 25)->best_stats.hp);
    assert(city_bestiary_recover(&restored, 25, persist, NULL) == CITY_BESTIARY_UNCHANGED);

    city_creature_stats_t lower_hp_better_total = {35, 70, 55};
    assert(city_bestiary_capture_with_stats(&restored, 2, 25, 2,
        &lower_hp_better_total, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_record_const(&restored, 25)->current_hp == 35U);

    assert(city_bestiary_capture(&restored, 3, 25, 2, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_capture(&restored, 4, 4, 1, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_record_const(&restored, 25)->capture_count == 3U);

    assert(city_bestiary_choose_buddy(&restored, 25, persist, NULL) == CITY_BESTIARY_APPLIED);
    before = restored;
    save_ok = false;
    assert(city_bestiary_release(&restored, 25, persist, NULL) == CITY_BESTIARY_STORAGE_FAILED);
    assert(memcmp(&restored, &before, sizeof(restored)) == 0);
    save_ok = true;
    assert(city_bestiary_release(&restored, 25, persist, NULL) == CITY_BESTIARY_APPLIED);
    const city_creature_record_t *released = city_bestiary_record_const(&restored, 25);
    assert(released->state == CITY_DISCOVERY_CAPTURED);
    assert(released->capture_count == 2U);
    assert(released->current_hp == 35U);
    assert(restored.buddy_species_id == 25U);
    assert(city_bestiary_record_const(&restored, 4)->capture_count == 1U);

    assert(city_bestiary_encode(&restored, encoded));
    assert(city_bestiary_decode(encoded, sizeof(encoded), &bestiary));
    assert(city_bestiary_record_const(&bestiary, 25)->capture_count == 2U);
    assert(city_bestiary_record_const(&bestiary, 4)->capture_count == 1U);

    assert(city_bestiary_release(&restored, 25, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_record_const(&restored, 25)->capture_count == 1U);
    assert(restored.buddy_species_id == 25U);
    assert(city_bestiary_release(&restored, 25, persist, NULL) == CITY_BESTIARY_APPLIED);
    released = city_bestiary_record_const(&restored, 25);
    assert(released->state == CITY_DISCOVERY_SEEN);
    assert(released->capture_count == 0);
    assert(released->current_hp == 0);
    assert(restored.buddy_species_id == 0);
    assert(city_bestiary_record_const(&restored, 4)->state == CITY_DISCOVERY_CAPTURED);
    assert(city_bestiary_record_const(&restored, 4)->capture_count == 1U);
    assert(city_bestiary_release(&restored, 25, persist, NULL) == CITY_BESTIARY_INVALID);

    assert(city_bestiary_capture(&restored, 5, 25, 2, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_record_const(&restored, 25)->current_hp ==
           city_bestiary_record_const(&restored, 25)->best_stats.hp);
    assert(city_bestiary_apply_damage(&restored, 25, UINT8_MAX, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_record_const(&restored, 25)->current_hp == 0);
    assert(city_bestiary_recover(&restored, 25, persist, NULL) == CITY_BESTIARY_APPLIED);

    assert(city_bestiary_encode(&restored, encoded));
    encoded[CITY_BESTIARY_HEADER_BYTES + city_species_index(25) * CITY_BESTIARY_RECORD_BYTES + 21U] = 1U;
    assert(!city_bestiary_decode(encoded, sizeof(encoded), &bestiary));

    city_bestiary_t evolved;
    city_bestiary_init(&evolved);
    assert(city_bestiary_capture(&evolved, 1, 1, 1, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_choose_buddy(&evolved, 1, persist, NULL) == CITY_BESTIARY_APPLIED);
    city_creature_record_t *source = city_bestiary_record(&evolved, 1);
    source->friendship = CITY_EVOLUTION_BOND;
    source->buddy_places = 7U;
    assert(city_bestiary_evolve(&evolved, 1, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_record_const(&evolved, 2)->capture_count == 0U);
    assert(city_bestiary_record_const(&evolved, 2)->evolution_obtained);
    assert(city_bestiary_release(&evolved, 2, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_record_const(&evolved, 2)->state == CITY_DISCOVERY_SEEN);
    assert(evolved.buddy_species_id == 0U);

    city_bestiary_t individuals;
    city_bestiary_init(&individuals);
    const city_creature_stats_t first_copy = {50, 55, 40};
    const city_creature_stats_t second_copy = {35, 70, 55};
    assert(city_bestiary_capture_with_stats(&individuals, 1, 25, 1,
        &first_copy, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_capture_with_stats(&individuals, 2, 25, 2,
        &second_copy, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_owned_count(&individuals, 25) == 2U);
    const city_owned_pokemon_t *copy_one = city_bestiary_owned_at(&individuals, 25, 0);
    const city_owned_pokemon_t *copy_two = city_bestiary_owned_at(&individuals, 25, 1);
    assert(copy_one && copy_two && copy_one->instance_id != copy_two->instance_id);
    assert(memcmp(&copy_one->stats, &first_copy, sizeof(first_copy)) == 0);
    const uint32_t release_id = copy_one->instance_id;
    assert(city_bestiary_release_instance(&individuals, release_id,
        persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_owned_count(&individuals, 25) == 1U);
    copy_two = city_bestiary_owned_at(&individuals, 25, 0);
    assert(copy_two && memcmp(&copy_two->stats, &second_copy, sizeof(second_copy)) == 0);
    assert(city_bestiary_record_const(&individuals, 25)->best_stats.attack == 70U);

    assert(writes > 0);
    return 0;
}
