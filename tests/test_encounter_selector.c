#include "encounter_selector.h"
#include "place_fingerprint.h"

#include <stdio.h>

static int failures;

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                    #condition);                                          \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static bool persist_ok(const city_bestiary_t *next, void *context)
{
    (void)next;
    (void)context;
    return true;
}

static void test_places_have_distinct_weight_profiles(void)
{
    unsigned total = 0;
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i) total += city_encounter_place_weight(1, city_species_id_at(i));
    CHECK(total == 32);
    CHECK(city_encounter_place_weight(1, CITY_SPECIES_MOSSBIT) == 0);
    CHECK(city_encounter_place_weight(1, CITY_SPECIES_BULBASAUR) == 6);
    CHECK(city_encounter_place_weight(2, CITY_SPECIES_CHARMANDER) == 6);
    CHECK(city_encounter_place_weight(3, CITY_SPECIES_SQUIRTLE) == 6);
    CHECK(city_encounter_place_weight(0, CITY_SPECIES_PIKACHU) == 0);
    CHECK(city_encounter_place_weight(CITY_PLACE_INVALID_ID, CITY_SPECIES_CHARMANDER) == 0);
}

static void test_new_place_prioritizes_unknown_species(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    CHECK(city_bestiary_mark_seen(
              &bestiary, CITY_SPECIES_CHARMANDER, persist_ok, NULL) ==
          CITY_BESTIARY_APPLIED);

    for (uint32_t seed = 0U; seed < 100U; ++seed) {
        city_encounter_selection_t selection;
        CHECK(city_encounter_select(
            1U, true, &bestiary, seed, &selection));
        CHECK(selection.species_id != CITY_SPECIES_CHARMANDER);
    }
}

static void test_known_place_uses_weighted_pool(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    unsigned int charmander = 0U;
    unsigned int bulbasaur = 0U;
    unsigned int squirtle = 0U;

    for (uint32_t seed = 0U; seed < 1000U; ++seed) {
        city_encounter_selection_t selection;
        CHECK(city_encounter_select(
            1U, false, &bestiary, seed, &selection));
        if (selection.species_id == CITY_SPECIES_CHARMANDER) {
            ++charmander;
        } else if (selection.species_id == CITY_SPECIES_BULBASAUR) {
            ++bulbasaur;
        } else if (selection.species_id == CITY_SPECIES_SQUIRTLE) {
            ++squirtle;
        }
    }
    CHECK(bulbasaur > charmander && bulbasaur > squirtle);
    CHECK(bulbasaur > 140U && bulbasaur < 240U);
}

static void test_stats_are_deterministic_and_vary(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    city_encounter_selection_t first;
    city_encounter_selection_t repeated;
    city_encounter_selection_t different;

    CHECK(city_encounter_select(2U, false, &bestiary, 44U, &first));
    CHECK(city_encounter_select(2U, false, &bestiary, 44U, &repeated));
    CHECK(first.species_id == repeated.species_id);
    CHECK(first.stats.hp == repeated.stats.hp);
    CHECK(first.stats.attack == repeated.stats.attack);
    CHECK(first.stats.defense == repeated.stats.defense);

    bool found_different = false;
    for (uint32_t seed = 45U; seed < 200U; ++seed) {
        CHECK(city_encounter_select(
            2U, false, &bestiary, seed, &different));
        if (different.species_id == first.species_id &&
            (different.stats.hp != first.stats.hp ||
             different.stats.attack != first.stats.attack ||
             different.stats.defense != first.stats.defense)) {
            found_different = true;
            break;
        }
    }
    CHECK(found_different);

    const city_species_definition_t *definition =
        city_species_definition(first.species_id);
    CHECK(first.stats.hp >= definition->base_hp);
    CHECK(first.stats.hp <= definition->base_hp + 15U);
    CHECK(first.stats.attack >= definition->base_attack);
    CHECK(first.stats.attack <= definition->base_attack + 15U);
    CHECK(first.stats.defense >= definition->base_defense);
    CHECK(first.stats.defense <= definition->base_defense + 15U);
}

int main(void)
{
    test_places_have_distinct_weight_profiles();
    test_new_place_prioritizes_unknown_species();
    test_known_place_uses_weighted_pool();
    test_stats_are_deterministic_and_vary();

    if (failures == 0) {
        puts("test_encounter_selector: ALL PASS");
        return 0;
    }
    fprintf(stderr, "test_encounter_selector: %d failure(s)\n", failures);
    return 1;
}
