#include "bestiary_service.h"
#include "game_loop.h"
#include "location_mode.h"
#include "place_profile_codec.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t bytes[CITY_BESTIARY_ENCODED_BYTES];
    bool written;
} memory_store_t;

static bool persist_bestiary(
    const city_bestiary_t *next,
    void *context)
{
    memory_store_t *store = context;
    store->written = city_bestiary_encode(next, store->bytes);
    return store->written;
}

static city_place_fingerprint_t demo_fingerprint(void)
{
    const uint64_t tokens[4] = {
        UINT64_C(0x1401),
        UINT64_C(0x1402),
        UINT64_C(0x1403),
        UINT64_C(0x1404),
    };
    city_place_fingerprint_t fingerprint;
    memset(&fingerprint, 0, sizeof(fingerprint));
    fingerprint.count = 4U;
    memcpy(fingerprint.tokens, tokens, sizeof(tokens));
    return fingerprint;
}

static bool confirm_demo_place(
    city_location_state_t *location,
    city_place_catalog_t *catalog)
{
    const city_place_fingerprint_t fingerprint = demo_fingerprint();
    city_location_input_t input = {
        .scan_status = CITY_SCAN_EVIDENCE,
        .now_ms = 0U,
        .fingerprint = &fingerprint,
        .catalog = catalog,
    };
    if (city_location_mode_step(location, &input).event !=
        CITY_LOCATION_EVENT_NEW_PENDING) {
        return false;
    }

    input.now_ms = CITY_LOCATION_CONFIRM_DELAY_MS;
    if (city_location_mode_step(location, &input).event !=
        CITY_LOCATION_EVENT_NEW_PLACE_READY) {
        return false;
    }

    catalog->count = 1U;
    catalog->places[0].schema_version =
        CITY_PLACE_PROFILE_SCHEMA_VERSION;
    catalog->places[0].place_id = 1U;
    catalog->places[0].confidence_permille = 1000U;
    catalog->places[0].last_confirmed_ms = input.now_ms;
    catalog->places[0].fingerprint = fingerprint;

    uint8_t encoded[CITY_PLACE_CATALOG_MAX_ENCODED_BYTES];
    size_t written = 0U;
    city_place_catalog_t persisted;
    memset(&persisted, 0, sizeof(persisted));
    if (!city_place_catalog_encode(
            catalog, encoded, sizeof(encoded), &written) ||
        !city_place_catalog_decode(encoded, written, &persisted)) {
        return false;
    }
    *catalog = persisted;
    return city_location_mode_commit_place(location, 1U, input.now_ms);
}

int main(void)
{
    city_location_state_t location;
    city_location_mode_init(&location);
    city_place_catalog_t catalog;
    memset(&catalog, 0, sizeof(catalog));
    if (!confirm_demo_place(&location, &catalog)) {
        fputs("{\"ok\":false,\"stage\":\"place\"}\n", stderr);
        return 1;
    }

    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    city_game_session_t game;
    city_game_init(&game);
    if (city_game_arrive(&game, 1U, UINT64_C(0x14000001), 25U) !=
            CITY_GAME_EVENT_ENCOUNTER_STARTED ||
        city_game_begin_capture(&game, 30000U) !=
            CITY_GAME_EVENT_CAPTURE_STARTED) {
        fputs("{\"ok\":false,\"stage\":\"encounter\"}\n", stderr);
        return 1;
    }

    memory_store_t store;
    memset(&store, 0, sizeof(store));
    const uint64_t throw_at =
        game.capture_round.started_ms +
        game.capture_round.target_center_ms;
    if (city_game_throw(
            &game,
            throw_at,
            &bestiary,
            persist_bestiary,
            &store) != CITY_GAME_EVENT_CAPTURED ||
        city_game_open_bestiary(&game) !=
            CITY_GAME_EVENT_BESTIARY_OPENED ||
        !store.written) {
        fputs("{\"ok\":false,\"stage\":\"capture\"}\n", stderr);
        return 1;
    }

    city_bestiary_t reloaded;
    city_bestiary_init(&reloaded);
    if (!city_bestiary_decode(
            store.bytes, sizeof(store.bytes), &reloaded) ||
        reloaded.charmander.state != CITY_DISCOVERY_CAPTURED ||
        reloaded.charmander.capture_count != 1U) {
        fputs("{\"ok\":false,\"stage\":\"bestiary\"}\n", stderr);
        return 1;
    }

    puts("{\"ok\":true,\"place\":1,\"encounter\":\"Charmander\","
         "\"capture\":\"caught\",\"bestiary_count\":1}");
    return 0;
}
