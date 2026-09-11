#include "passport_progress.h"
#include <stddef.h>
#include <string.h>

static bool stamps_valid(const city_passport_stamps_t *s)
{
    if (s == NULL || s->count > CITY_PLACE_MAX_COUNT) return false;
    for (uint16_t i = 0; i < s->count; ++i) {
        if (s->place_ids[i] == CITY_WILD_PLACE_ID || s->place_ids[i] == CITY_PLACE_INVALID_ID) return false;
        for (uint16_t j = 0; j < i; ++j)
            if (s->place_ids[j] == s->place_ids[i]) return false;
    }
    return true;
}
bool city_passport_stamps_from_catalog(const city_place_catalog_t *catalog,
                                      city_passport_stamps_t *stamps)
{
    if (catalog == NULL || stamps == NULL || catalog->count > CITY_PLACE_MAX_COUNT) return false;
    city_passport_stamps_t next = {.count = catalog->count};
    for (uint16_t i = 0; i < catalog->count; ++i) next.place_ids[i] = catalog->places[i].place_id;
    if (!stamps_valid(&next)) return false;
    // Stable order even if the catalog storage order changes.
    for (uint16_t i = 1; i < next.count; ++i) {
        const uint16_t id = next.place_ids[i];
        uint16_t j = i;
        while (j > 0 && next.place_ids[j - 1] > id) {
            next.place_ids[j] = next.place_ids[j - 1]; --j;
        }
        next.place_ids[j] = id;
    }
    *stamps = next;
    return true;
}
city_passport_progress_t city_passport_progress(const city_passport_stamps_t *stamps,
                                              const city_bestiary_t *bestiary)
{
    city_passport_progress_t p = {.pages = 1};
    p.places_ready = stamps_valid(stamps);
    p.collection_ready = city_bestiary_is_valid(bestiary);
    if (p.places_ready) {
        p.places = stamps->count;
        if (p.places) p.pages = (uint8_t)((p.places + CITY_PASSPORT_STAMPS_PER_PAGE - 1) / CITY_PASSPORT_STAMPS_PER_PAGE);
    }
    if (p.collection_ready) {
        p.discovered = city_bestiary_discovered_count(bestiary);
        p.captured = city_bestiary_captured_count(bestiary);
    }
    if (!p.places_ready || !p.collection_ready) return p;
    if (p.captured == 0) { p.goal = CITY_PASSPORT_FIRST_CAPTURE; return p; }
    if (p.places < 2) { p.goal = CITY_PASSPORT_NEW_PLACE; p.target = p.places + 1; return p; }
    if (p.captured < CITY_SPECIES_COUNT) {
        p.goal = CITY_PASSPORT_CATCH_SPECIES;
        for (unsigned i = 0; i < CITY_SPECIES_COUNT; ++i) {
            const city_creature_record_t *r = city_bestiary_record_const(bestiary, city_species_id_at(i));
            if (r->state != CITY_DISCOVERY_CAPTURED) {
                p.target = city_species_id_at(i); p.target_seen = r->state == CITY_DISCOVERY_SEEN; break;
            }
        }
        return p;
    }
    const uint16_t milestones[] = {3, 5, 10, CITY_PLACE_MAX_COUNT};
    for (unsigned i = 0; i < sizeof(milestones) / sizeof(milestones[0]); ++i) {
        if (p.places < milestones[i]) {
            p.goal = CITY_PASSPORT_NEW_PLACE; p.target = milestones[i]; return p;
        }
    }
    p.goal = CITY_PASSPORT_COMPLETE;
    return p;
}
uint8_t city_passport_turn_page(uint8_t page, uint8_t pages, bool forward)
{
    if (pages == 0 || page >= pages) return 0;
    return forward ? (uint8_t)((page + 1U) % pages)
                   : (uint8_t)(page == 0 ? pages - 1 : page - 1);
}
