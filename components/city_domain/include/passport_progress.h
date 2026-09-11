#pragma once
#include "bestiary_service.h"
#include "place_fingerprint.h"
#define CITY_PASSPORT_STAMPS_PER_PAGE 6U
// Contains only anonymous IDs, never wireless evidence or timestamps.
typedef struct {
    uint16_t count;
    uint16_t place_ids[CITY_PLACE_MAX_COUNT];
} city_passport_stamps_t;
typedef enum {
    CITY_PASSPORT_DATA_UNAVAILABLE = 0,
    CITY_PASSPORT_FIRST_CAPTURE,
    CITY_PASSPORT_NEW_PLACE,
    CITY_PASSPORT_CATCH_SPECIES,
    CITY_PASSPORT_COMPLETE,
} city_passport_goal_t;
typedef struct {
    bool places_ready;
    bool collection_ready;
    uint16_t places;
    uint8_t discovered;
    uint8_t captured;
    uint8_t pages;
    city_passport_goal_t goal;
    uint16_t target;
    bool target_seen;
} city_passport_progress_t;
bool city_passport_stamps_from_catalog(const city_place_catalog_t *catalog,
                                      city_passport_stamps_t *stamps);
city_passport_progress_t city_passport_progress(const city_passport_stamps_t *stamps,
                                              const city_bestiary_t *bestiary);
uint8_t city_passport_turn_page(uint8_t page, uint8_t pages, bool forward);
