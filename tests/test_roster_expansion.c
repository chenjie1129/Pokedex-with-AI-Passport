#include "bestiary_service.h"
#include "encounter_selector.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool ok = true;
static bool persist(const city_bestiary_t *b, void *ctx) { (void)b; (void)ctx; return ok; }
static void crc(uint8_t *p, size_t len)
{
    uint32_t c = UINT32_MAX;
    for (size_t i = 0; i < len - 4; ++i) {
        c ^= p[i]; for (unsigned j = 0; j < 8; ++j) c = (c >> 1) ^ (0xedb88320U & (uint32_t)-(int32_t)(c & 1));
    }
    c = ~c; for (unsigned i = 0; i < 4; ++i) p[len-4+i] = (uint8_t)(c >> (i*8));
}
int main(void)
{
    assert(CITY_SPECIES_COUNT == 12);
    city_bestiary_t b; city_bestiary_init(&b);
    city_discovery_state_t before;
    assert(city_bestiary_encounter_status(&b, CITY_SPECIES_EEVEE, &before) && before == CITY_DISCOVERY_UNKNOWN);
    ok = false;
    assert(city_bestiary_mark_seen(&b, CITY_SPECIES_EEVEE, persist, NULL) == CITY_BESTIARY_STORAGE_FAILED);
    assert(b.records[11].state == CITY_DISCOVERY_UNKNOWN);
    ok = true;
    assert(city_bestiary_mark_seen(&b, CITY_SPECIES_EEVEE, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(before == CITY_DISCOVERY_UNKNOWN); // Snapshot remains NEW after discovery save.
    assert(city_bestiary_encounter_status(&b, CITY_SPECIES_EEVEE, &before) && before == CITY_DISCOVERY_SEEN);
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i) {
        const uint16_t id = city_species_id_at(i);
        assert(city_species_index(id) == i);
        assert(city_bestiary_capture(&b, i+1, id, 2, persist, NULL) == CITY_BESTIARY_APPLIED);
        assert(city_bestiary_encounter_status(&b, id, &before) && before == CITY_DISCOVERY_CAPTURED);
    }
    assert(city_bestiary_captured_count(&b) == 12);
    uint8_t bytes[CITY_BESTIARY_ENCODED_BYTES]; assert(city_bestiary_encode(&b, bytes));
    city_bestiary_t restored; assert(city_bestiary_decode(bytes, sizeof(bytes), &restored));
    assert(memcmp(&b, &restored, sizeof(b)) == 0);
    assert(!city_bestiary_encounter_status(&b, 999, &before));
    // Real v4/v5 byte layout, all original counts/stats and cooldown preserved.
    uint8_t legacy[156] = {0};
    memcpy(legacy, bytes, 16); legacy[6] = 3; legacy[7] = 0;
    memcpy(legacy+16, bytes+32, 60);
    for (unsigned version = 4; version <= 5; ++version) {
        legacy[4] = version; legacy[76] = version == 5; crc(legacy, sizeof(legacy));
        assert(city_bestiary_decode(legacy, sizeof(legacy), &restored));
        assert(restored.wild_cooldown_active == (version == 5));
        assert(restored.last_settled_sequence == 12);
        for (unsigned i = 0; i < 3; ++i) assert(memcmp(&restored.records[i], &b.records[i], sizeof(b.records[i])) == 0);
        for (unsigned i = 3; i < 12; ++i) assert(restored.records[i].state == CITY_DISCOVERY_UNKNOWN);
    }
    // A v6 catalog can grow without depending on positional record order.
    uint8_t short_save[96] = {0}; // 32 header + 3*20 records + 4 CRC
    memcpy(short_save, bytes, 32); short_save[6] = 3;
    memcpy(short_save+32, bytes+72, 20); memcpy(short_save+52, bytes+32, 20); memcpy(short_save+72, bytes+52, 20);
    crc(short_save,sizeof(short_save)); assert(city_bestiary_decode(short_save,sizeof(short_save),&restored));
    assert(restored.records[0].capture_count == 1 && restored.records[2].capture_count == 1);
    memcpy(bytes+52,bytes+32,20); crc(bytes,sizeof(bytes));
    assert(!city_bestiary_decode(bytes,sizeof(bytes),&restored)); // Duplicate IDs rejected.
    city_bestiary_init(&b);
    unsigned hits[12]={0}, wild[12]={0};
    for (uint32_t seed = 0; seed < 32000; ++seed) {
        city_encounter_selection_t e;
        assert(city_encounter_select(1,false,&b,seed,&e)); ++hits[city_species_index(e.species_id)];
        assert(city_wild_encounter_select(seed,&e)); ++wild[city_species_index(e.species_id)];
    }
    for (unsigned i = 0; i < 12; ++i) {
        unsigned expected = city_species_definition(city_species_id_at(i))->place_pool == 0 ? 6000 : 1000;
        assert(hits[i] > expected*85/100 && hits[i] < expected*115/100);
        assert((wild[i] > 0) == city_species_definition(city_species_id_at(i))->wild_eligible);
    }
    for (unsigned i = 0; i < 12; ++i) assert(city_bestiary_mark_seen(&b,city_species_id_at(i),persist,NULL)==CITY_BESTIARY_APPLIED);
    for (unsigned i = 0; i < 11; ++i) assert(city_bestiary_capture(&b,i+1,city_species_id_at(i),1,persist,NULL)==CITY_BESTIARY_APPLIED);
    for (unsigned seed = 0; seed < 100; ++seed) {
        city_encounter_selection_t e; assert(city_encounter_select(2,true,&b,seed,&e));
        assert(e.species_id == CITY_SPECIES_EEVEE); // Seen-but-uncaught priority.
    }
    puts("Roster: migration, pre-encounter status, all species and probability pools passed");
    return 0;
}
