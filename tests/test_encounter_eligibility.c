#include "place_scan_policy.h"
#include "encounter_selector.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned writes;
static bool save(const city_bestiary_t *next, void *context)
{
    (void)next; (void)context; ++writes; return true;
}

static city_location_output_t observe(city_location_state_t *state,
    const city_place_catalog_t *catalog, city_scan_status_t status,
    const city_place_fingerprint_t *fp, uint64_t now)
{
    const city_location_input_t input = {
        .scan_status=status, .fingerprint=fp, .catalog=catalog, .now_ms=now,
    };
    return city_location_mode_step(state, &input);
}

int main(void)
{
    const city_place_fingerprint_t known = {.tokens={1,2,3,4}, .count=4};
    const city_place_fingerprint_t gray = {.tokens={1,2,5,6}, .count=4};
    const city_place_fingerprint_t sparse = {.tokens={1,2,3}, .count=3};
    const city_place_fingerprint_t other = {.tokens={11,12,13,14}, .count=4};
    const city_place_fingerprint_t changed = {.tokens={21,22,23,24}, .count=4};
    city_place_catalog_t catalog = {.count=1};
    catalog.places[0].place_id=1;
    catalog.places[0].fingerprint=known;
    city_location_state_t state;
    city_location_mode_init(&state);
    city_location_output_t output=observe(&state,&catalog,CITY_SCAN_EVIDENCE,&known,0);
    assert(city_place_scan_decide(&output,false).encounter_eligible);
    const city_location_state_t before=state;
    const city_scan_status_t statuses[] = {CITY_SCAN_EVIDENCE,CITY_SCAN_EMPTY,CITY_SCAN_EVIDENCE,CITY_SCAN_ERROR};
    const city_place_fingerprint_t *fingerprints[] = {&gray,NULL,&sparse,NULL};
    const place_result_kind_t expected[] = {PLACE_RESULT_GRAY,PLACE_RESULT_WILD,PLACE_RESULT_WILD,PLACE_RESULT_SCAN_ERROR};
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    for (unsigned i=0;i<4;i++) {
        output=observe(&state,&catalog,statuses[i],fingerprints[i],1000+i);
        city_place_scan_decision_t decision=city_place_scan_decide(&output,false);
        assert(decision.kind==expected[i]);
        assert(!decision.encounter_eligible);
        assert(!output.encounter_eligible);
        assert(memcmp(&state,&before,sizeof(state))==0);
        if (decision.encounter_eligible) {
            (void)city_bestiary_mark_seen(&bestiary,CITY_SPECIES_CHARMANDER,save,NULL);
        }
    }
    assert(writes==0);
    assert(city_bestiary_discovered_count(&bestiary)==0);
    output=observe(&state,&catalog,CITY_SCAN_EVIDENCE,&known,2000);
    assert(output.event==CITY_LOCATION_EVENT_LOCKED);
    assert(city_place_scan_decide(&output,false).encounter_eligible);
    assert(state.locked_until_ms==before.locked_until_ms);
    output.encounter_eligible=false;
    assert(!city_place_scan_decide(&output,false).encounter_eligible);
    output=observe(&state,&catalog,CITY_SCAN_EVIDENCE,&other,3000);
    assert(output.event==CITY_LOCATION_EVENT_NEW_PENDING);
    assert(!city_place_scan_decide(&output,false).encounter_eligible);
    output=observe(&state,&catalog,CITY_SCAN_EVIDENCE,&other,23000);
    assert(output.event==CITY_LOCATION_EVENT_NEW_PLACE_READY);
    assert(!output.encounter_eligible);
    assert(city_place_scan_decide(&output,false).kind==PLACE_RESULT_STORAGE_ERROR);
    assert(!city_place_scan_decide(&output,false).encounter_eligible);
    assert(city_place_scan_decide(&output,true).kind==PLACE_RESULT_NEW_CONFIRMED);
    assert(city_place_scan_decide(&output,true).encounter_eligible);
    assert(!city_place_scan_decide(NULL,true).encounter_eligible);
    city_location_mode_init(&state);
    output=observe(&state,&catalog,CITY_SCAN_EVIDENCE,&gray,400000);
    assert(!city_place_scan_decide(&output,true).encounter_eligible);
    city_location_mode_init(&state);
    output=observe(&state,&catalog,CITY_SCAN_EVIDENCE,&other,500000);
    assert(output.event==CITY_LOCATION_EVENT_NEW_PENDING);
    output=observe(&state,&catalog,CITY_SCAN_EVIDENCE,&changed,520000);
    assert(output.event==CITY_LOCATION_EVENT_NEW_UNSTABLE);
    assert(city_place_scan_decide(&output,false).kind==PLACE_RESULT_UNSTABLE);
    assert(!city_place_scan_decide(&output,false).encounter_eligible);
    puts("encounter eligibility: gray/empty/sparse/error denied, fresh evidence and durable new places allowed");
    return 0;
}
