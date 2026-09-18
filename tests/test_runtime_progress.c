#include "catalog_provider.h"
#include "encounter_selector.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool at(void *ctx, uint32_t i, city_species_definition_t *out)
{
    (void)ctx;
    if (i < CITY_SPECIES_COUNT-1) { *out = *CITY_CATALOG_DEFAULT.at((uint8_t)i); return true; }
    if (i == 9999) { *out = *CITY_CATALOG_DEFAULT.at(CITY_SPECIES_COUNT-1); return true; }
    if (i >= 10000) return false;
    *out = (city_species_definition_t){.species_id=(uint16_t)(1000+i), .base_hp=45,
        .base_attack=40, .base_defense=50, .type1=5, .wild_eligible=true, .place_eligible=true};
    return true;
}
static bool find(void *ctx, uint16_t id, city_species_definition_t *out)
{
    const city_species_definition_t *d = city_species_definition(id);
    if (d) { *out = *d; return true; }
    return id >= 1000+CITY_SPECIES_COUNT-1 && id < 10999 && at(ctx, id-1000, out);
}
static bool persist(const city_bestiary_t *next, void *ctx)
{ return !ctx && city_bestiary_is_valid(next); }
int main(void)
{
    city_runtime_catalog_t p = {NULL, 10000, at, find};
    assert(city_catalog_bind(&p)); assert(city_species_count()==10000);
    assert(city_species_position(1100)==100 && city_species_position(60000)==9999);
    city_bestiary_t b; city_bestiary_init(&b);
    const uint16_t id = 1100;
    city_bestiary_t before=b;
    assert(city_bestiary_record_const(&b,id)->state==CITY_DISCOVERY_UNKNOWN);
    assert(city_bestiary_mark_seen(&b,id,persist,&b)==CITY_BESTIARY_STORAGE_FAILED);
    assert(!memcmp(&before,&b,sizeof(b)));
    assert(city_bestiary_capture(&b,1,id,1,persist,&b)==CITY_BESTIARY_STORAGE_FAILED);
    assert(!memcmp(&before,&b,sizeof(b)));
    assert(city_bestiary_capture(&b,1,id,1,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_capture(&b,2,id,2,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_choose_buddy_instance(&b,2,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_damage_instance(&b,2,7,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_visit(&b,3,2,2,1200,persist,NULL)==CITY_BESTIARY_APPLIED);
    uint8_t encoded[CITY_BESTIARY_STORAGE_MAX_BYTES], encoded2[sizeof(encoded)]; size_t n,m;
    assert(city_bestiary_encode_sparse(&b,encoded,sizeof(encoded),&n));
    city_bestiary_t decoded; assert(city_bestiary_decode(encoded,n,&decoded));
    assert(city_bestiary_encode_sparse(&decoded,encoded2,sizeof(encoded2),&m));
    assert(n==m && !memcmp(encoded,encoded2,n));
    assert(decoded.owned[1].current_hp==38 && decoded.buddy_instance_id==2);
    assert(!city_bestiary_encode(&b,encoded2)); /* Never silently drop package progress. */
    city_encounter_selection_t selection; bool package=false;
    for (unsigned i=0;i<10;++i) { assert(city_wild_encounter_select(i,&selection)); package|=selection.species_id>=1000; }
    assert(package); assert(city_encounter_select(1,true,&b,7,&selection));
    assert(city_bestiary_release_instance(&b,1,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_release_instance(&b,2,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_record_const(&b,id)->state==CITY_DISCOVERY_SEEN);
    /* Fixed progress capacity; a failed discovery cannot allocate published state. */
    for (unsigned i=0;i<CITY_MAX_PROGRESS_RECORDS-CITY_SPECIES_COUNT-2;++i)
        assert(city_bestiary_mark_seen(&b,(uint16_t)(2000+i),persist,NULL)==CITY_BESTIARY_APPLIED);
    before=b;
    assert(city_bestiary_mark_seen(&b,3000,persist,NULL)==CITY_BESTIARY_COUNTER_FULL);
    assert(!memcmp(&b,&before,sizeof(b)));
    assert(city_bestiary_capture(&b,4,3000,1,persist,NULL)==CITY_BESTIARY_COUNTER_FULL);
    assert(!memcmp(&b,&before,sizeof(b)));
    assert(city_catalog_bind(NULL));
    before=decoded; assert(!city_bestiary_decode(encoded,n,&decoded));
    assert(!memcmp(&before,&decoded,sizeof(decoded))); /* Missing catalog preserves output. */
    printf("10000-species runtime: bounded %zu-byte model, sparse copies, failures and missing-pack rejection passed\n",sizeof(b));
}
