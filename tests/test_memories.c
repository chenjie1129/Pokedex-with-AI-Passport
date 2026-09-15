#include "bestiary_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t wire[CITY_BESTIARY_ENCODED_BYTES];
static bool fail;
static unsigned writes;
static bool save(const city_bestiary_t *b, void *ctx)
{
    (void)ctx; ++writes;
    assert(city_bestiary_is_valid(b));
    if (fail) return false;
    return city_bestiary_encode(b, wire);
}
static void crc(uint8_t *p, size_t n)
{
    uint32_t c=UINT32_MAX;
    for(size_t i=0;i<n-4;++i){c^=p[i];for(unsigned j=0;j<8;++j)c=(c>>1)^((0U-(c&1U))&UINT32_C(0xedb88320));}
    c=~c; for(unsigned j=0;j<4;++j)p[n-4+j]=(uint8_t)(c>>(8*j));
}
int main(void)
{
    city_bestiary_t b, before, loaded;
    city_bestiary_init(&b);
    assert(city_bestiary_capture(&b,1,25,1,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_capture(&b,2,25,1,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_choose_buddy_instance(&b,1,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(!b.owned[0].memory_count);
    assert(city_companion_invitation(&b.owned[0])==CITY_INVITE_FIRST_OUTING);
    before=b;fail=true;
    assert(city_bestiary_visit(&b,3,1,1,4,save,NULL)==CITY_BESTIARY_STORAGE_FAILED);
    assert(!memcmp(&b,&before,sizeof b));fail=false;
    assert(city_bestiary_visit(&b,3,1,1,4,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_decode(wire,sizeof wire,&b));
    assert(b.owned[0].memory_count==1 && b.owned[0].memories[0].kind==CITY_MEMORY_FIRST_OUTING);
    assert(b.owned[0].memories[0].place==1 && !b.owned[1].memory_count);
    unsigned w=writes;
    assert(city_bestiary_visit(&b,3,1,1,4,save,NULL)==CITY_BESTIARY_DUPLICATE);
    assert(city_bestiary_visit(&b,4,1,1,4,save,NULL)==CITY_BESTIARY_UNCHANGED && writes==w);
    assert(city_bestiary_choose_buddy_instance(&b,2,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_visit(&b,4,1,2,4,save,NULL)==CITY_BESTIARY_INVALID);
    assert(!b.owned[1].memory_count);
    assert(city_bestiary_choose_buddy_instance(&b,1,save,NULL)==CITY_BESTIARY_APPLIED);
    for(unsigned place=2;place<=4;++place)
        assert(city_bestiary_visit(&b,place+3,1,place,4,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(b.owned[0].memory_count==5 && b.owned[0].memories[4].kind==CITY_MEMORY_FAMILIAR);
    assert(city_companion_place_count(&b.owned[0])==4);
    assert(city_companion_invitation(&b.owned[0])==CITY_INVITE_NEW_PLACE);
    assert(city_bestiary_visit(&b,8,1,1,4,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(b.owned[0].memory_count==5 && b.owned[0].memories[0].kind==CITY_MEMORY_NEW_PLACE);
    assert(b.owned[0].memories[4].kind==CITY_MEMORY_REVISIT);
    assert(city_bestiary_damage_instance(&b,1,2,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_companion_invitation(&b.owned[0])==CITY_INVITE_REST);
    before=b;fail=true;
    assert(city_bestiary_recover_instance(&b,1,save,NULL)==CITY_BESTIARY_STORAGE_FAILED);
    assert(!memcmp(&b,&before,sizeof b));fail=false;
    assert(city_bestiary_recover_instance(&b,1,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(b.owned[0].memories[4].kind==CITY_MEMORY_REST);
    w=writes;assert(city_bestiary_recover_instance(&b,1,save,NULL)==CITY_BESTIARY_UNCHANGED && w==writes);
    b.owned[0].friendship=59;
    assert(city_bestiary_visit(&b,9,1,2,4,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(b.owned[0].memories[4].kind==CITY_MEMORY_CLOSE);
    b.owned[0].friendship=100;
    assert(city_bestiary_visit(&b,10,1,3,4,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(b.owned[0].memories[4].kind==CITY_MEMORY_REVISIT);
    assert(city_bestiary_decode(wire,sizeof wire,&loaded) && !memcmp(&b,&loaded,sizeof b));
    /* A full-capacity schema-11 migration preserves every existing byte of each copy. */
    city_bestiary_init(&b);
    for(unsigned i=0;i<CITY_MAX_OWNED_POKEMON;++i)
        assert(city_bestiary_capture(&b,i+1,25,1,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_choose_buddy_instance(&b,100,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_visit(&b,161,100,4,4,save,NULL)==CITY_BESTIARY_APPLIED);
    enum { OFFSET=48+CITY_SPECIES_COUNT*22, OLD_SIZE=OFFSET+160*20+4 };
    uint8_t old[OLD_SIZE];memset(old,0,sizeof old);memcpy(old,wire,OFFSET);old[4]=11;
    for(unsigned i=0;i<160;++i)memcpy(old+OFFSET+i*20,wire+OFFSET+i*CITY_OWNED_POKEMON_BYTES,20);
    crc(old,sizeof old);before=b;
    assert(city_bestiary_decode(old,sizeof old,&b));
    assert(b.buddy_instance_id==100 && b.last_visit_sequence==161);
    assert(!memcmp(b.records,before.records,sizeof b.records));
    assert(city_bestiary_encode(&b,wire));
    for(unsigned i=0;i<160;++i){
        assert(!memcmp(old+OFFSET+i*20,wire+OFFSET+i*CITY_OWNED_POKEMON_BYTES,20));
        assert(!b.owned[i].memory_count);
    }
    /* Migrated history cannot be relabeled as a first outing. */
    assert(city_bestiary_visit(&b,162,100,5,4,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(b.owned[99].memories[0].kind==CITY_MEMORY_NEW_PLACE);
    before=b;wire[OFFSET+99*CITY_OWNED_POKEMON_BYTES+20]=6;crc(wire,sizeof wire);
    assert(!city_bestiary_decode(wire,sizeof wire,&b) && !memcmp(&b,&before,sizeof b));
    assert(city_bestiary_encode(&b,wire));wire[OFFSET+99*CITY_OWNED_POKEMON_BYTES+21]=99;crc(wire,sizeof wire);
    assert(!city_bestiary_decode(wire,sizeof wire,&b));
    assert(city_bestiary_release_instance(&b,100,save,NULL)==CITY_BESTIARY_APPLIED);
    assert(!b.buddy_instance_id && !city_bestiary_owned_by_id(&b,100));
    assert(city_bestiary_decode(wire,sizeof wire,&loaded));
    puts("Memories: migration, copy isolation, rollback, reboot, bounded history and milestones passed");
}
