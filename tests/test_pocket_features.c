#include "bestiary_service.h"
#include "encounter_selector.h"
#include "pocket_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool save_ok;
static unsigned writes;
static uint8_t disk[CITY_BESTIARY_ENCODED_BYTES];
static bool persist(const city_bestiary_t *next, void *unused)
{
    (void)unused;
    ++writes;
    return save_ok && city_bestiary_encode(next, disk);
}
static void checksum(void)
{
    uint32_t crc = UINT32_MAX;
    for (unsigned i = 0; i < 152; ++i) {
        crc ^= disk[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xedb88320U & (uint32_t)-(int32_t)(crc & 1));
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4; ++i) disk[152+i] = (uint8_t)(crc >> (i*8));
}
static void reboot(city_bestiary_t *b, city_wild_reward_guard_t *g)
{
    assert(city_bestiary_decode(disk, sizeof(disk), b));
    const city_wild_reward_snapshot_t snapshot = {
        .schema_version = CITY_WILD_REWARD_SCHEMA_VERSION,
        .cooldown_active = b->wild_cooldown_active,
    };
    city_wild_reward_guard_init(g, &snapshot, 0);
}
static void test_wild_transaction(void)
{
    city_bestiary_t b;
    city_bestiary_init(&b);
    city_wild_reward_guard_t g;
    city_wild_reward_guard_init(&g, NULL, 0);
    save_ok = false;
    assert(city_bestiary_reserve_wild(&b, &g, 100, CITY_SPECIES_BULBASAUR, persist, NULL) == CITY_BESTIARY_STORAGE_FAILED);
    assert(!b.wild_cooldown_active && !g.blocked);
    assert(b.bulbasaur.state == CITY_DISCOVERY_UNKNOWN);
    save_ok = true;
    assert(city_bestiary_reserve_wild(&b, &g, 100, CITY_SPECIES_CHARMANDER, persist, NULL) == CITY_BESTIARY_INVALID);
    assert(city_bestiary_reserve_wild(&b, &g, 100, CITY_SPECIES_BULBASAUR, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(b.wild_cooldown_active && b.bulbasaur.state == CITY_DISCOVERY_SEEN);
    assert(city_wild_reward_remaining_ms(&g, 101) == CITY_WILD_REWARD_COOLDOWN_MS - 1);
    const unsigned before = writes;
    assert(city_bestiary_reserve_wild(&b, &g, 101, CITY_SPECIES_SQUIRTLE, persist, NULL) == CITY_BESTIARY_COOLDOWN);
    assert(writes == before);
    // Reservation alone survives leaving, power loss, and repeated reboot.
    reboot(&b, &g);
    assert(city_wild_reward_remaining_ms(&g, 0) == CITY_WILD_REWARD_COOLDOWN_MS);
    reboot(&b, &g);
    assert(!city_wild_reward_available(&g, CITY_WILD_REWARD_COOLDOWN_MS - 1));
    city_encounter_selection_t selection;
    assert(city_wild_encounter_select(42, &selection));
    assert(city_bestiary_capture_with_stats(&b, 1, selection.species_id, CITY_WILD_PLACE_ID,
        &selection.stats, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_record_const(&b, selection.species_id)->last_place_id == CITY_WILD_PLACE_ID);
    assert(city_bestiary_capture_with_stats(&b, 1, selection.species_id, CITY_WILD_PLACE_ID,
        &selection.stats, persist, NULL) == CITY_BESTIARY_DUPLICATE);
    assert(city_bestiary_capture(&b, 2, CITY_SPECIES_CHARMANDER, 7, persist, NULL) == CITY_BESTIARY_APPLIED);
    reboot(&b, &g);
    assert(b.wild_cooldown_active && b.last_settled_sequence == 2);
    assert(city_bestiary_clear_wild_cooldown(&b, &g, 1, persist, NULL) == CITY_BESTIARY_COOLDOWN);
    save_ok = false;
    assert(city_bestiary_clear_wild_cooldown(&b, &g, CITY_WILD_REWARD_COOLDOWN_MS, persist, NULL) == CITY_BESTIARY_STORAGE_FAILED);
    assert(b.wild_cooldown_active);
    reboot(&b, &g);
    assert(g.blocked);
    save_ok = true;
    assert(city_bestiary_clear_wild_cooldown(&b, &g, CITY_WILD_REWARD_COOLDOWN_MS, persist, NULL) == CITY_BESTIARY_APPLIED);
    reboot(&b, &g);
    assert(!b.wild_cooldown_active && !g.blocked);
    const unsigned cleared = writes;
    assert(city_bestiary_clear_wild_cooldown(&b, &g, 0, persist, NULL) == CITY_BESTIARY_UNCHANGED);
    assert(writes == cleared);
    // Actual v4 wire layout migrates preserving every record and sequence.
    const city_bestiary_t expected = b;
    disk[4] = 4; disk[5] = 0; checksum();
    reboot(&b, &g);
    assert(b.schema_version == 5 && !b.wild_cooldown_active);
    assert(memcmp(&b.bulbasaur, &expected.bulbasaur, sizeof(b.bulbasaur)) == 0);
    assert(memcmp(&b.charmander, &expected.charmander, sizeof(b.charmander)) == 0);
    assert(memcmp(&b.squirtle, &expected.squirtle, sizeof(b.squirtle)) == 0);
    assert(b.last_settled_sequence == expected.last_settled_sequence);
    disk[4] = 5; disk[76] = 2; checksum();
    assert(!city_bestiary_decode(disk, sizeof(disk), &b));
}
static void test_pool(void)
{
    unsigned bulbasaur = 0;
    for (uint32_t seed = 0; seed < 10000; ++seed) {
        city_encounter_selection_t a, b;
        assert(city_wild_encounter_select(seed, &a));
        assert(city_wild_encounter_select(seed, &b));
        assert(a.species_id == b.species_id && a.stats.hp == b.stats.hp);
        assert(a.species_id == CITY_SPECIES_BULBASAUR || a.species_id == CITY_SPECIES_SQUIRTLE);
        if (a.species_id == CITY_SPECIES_BULBASAUR) ++bulbasaur;
    }
    assert(bulbasaur > 6500 && bulbasaur < 7500);
    assert(!city_wild_encounter_select(0, NULL));
}
static void test_power(void)
{
    assert(!city_battery_low(-1) && !city_battery_critical(-1));
    assert(!city_battery_low(101) && !city_battery_critical(101));
    assert(city_battery_low(10) && !city_battery_low(11));
    assert(city_battery_critical(5) && !city_battery_critical(6));
    city_pocket_policy_t p = {0};
    assert(!city_pocket_idle(&p, 59999, false, 80));
    assert(city_pocket_idle(&p, 60000, false, 80));
    assert(p.screen_off);
    assert(city_pocket_press(&p, 61000));
    assert(!p.screen_off && p.consume_gesture); // Click/long from wake are discarded.
    assert(!city_pocket_press(&p, 62000));
    assert(!p.consume_gesture); // Next independent press works normally.
    assert(!city_pocket_idle(&p, 100000, true, 5)); // Never interrupt scan/capture/save.
    assert(!city_pocket_idle(&p, 114999, false, 5));
    assert(city_pocket_idle(&p, 115000, false, 5));
    city_pocket_press(&p, 120000);
    city_pocket_sleep(&p); // Long-UP's trailing gesture cannot wake or navigate.
    assert(p.screen_off && p.consume_gesture);
    assert(city_pocket_press(&p, 121000));
    assert(p.consume_gesture);
    assert(!city_pocket_idle(&p, 0, false, 5)); // Clock regression cannot underflow.
}
int main(void)
{
    test_wild_transaction(); test_pool(); test_power();
    puts("Pocket features: transaction, migration, pool and wake policy passed");
    return 0;
}
