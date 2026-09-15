#include "bestiary_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool fail;
static unsigned writes;
static uint8_t saved[CITY_BESTIARY_ENCODED_BYTES];
static bool persist(const city_bestiary_t *next, void *ctx)
{
    (void)ctx; ++writes;
    assert(city_bestiary_is_valid(next));
    if (fail) return false;
    return city_bestiary_encode(next, saved);
}
static uint32_t crc(const uint8_t *p, size_t n)
{
    uint32_t c=UINT32_MAX;
    for(size_t i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^((0U-(c&1U))&UINT32_C(0xedb88320));}
    return ~c;
}
static void checksum(uint8_t *p,size_t n)
{uint32_t c=crc(p,n-4);for(unsigned i=0;i<4;++i)p[n-4+i]=(uint8_t)(c>>(8*i));}
static void reload(city_bestiary_t *b)
{ assert(city_bestiary_decode(saved,sizeof(saved),b)); }
int main(void)
{
    city_bestiary_t b, before;
    city_bestiary_init(&b);
    const city_creature_stats_t stats={35,55,40};
    assert(city_bestiary_capture_personality(&b,1,25,1,&stats,0,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_capture_personality(&b,2,25,1,&stats,5,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(b.owned[0].personality==0 && b.owned[1].personality==5);
    assert(city_bestiary_choose_buddy_instance(&b,2,persist,NULL)==CITY_BESTIARY_APPLIED);
    before=b;fail=true;
    assert(city_bestiary_choose_buddy_instance(&b,1,persist,NULL)==CITY_BESTIARY_STORAGE_FAILED);
    assert(!memcmp(&before,&b,sizeof(b)));fail=false;
    assert(city_bestiary_damage_instance(&b,2,7,persist,NULL)==CITY_BESTIARY_APPLIED);
    reload(&b);assert(b.buddy_instance_id==2 && b.owned[0].current_hp==35 && b.owned[1].current_hp==28);
    before=b;fail=true;
    assert(city_bestiary_recover_instance(&b,2,persist,NULL)==CITY_BESTIARY_STORAGE_FAILED);
    assert(!memcmp(&before,&b,sizeof(b)));fail=false;
    assert(city_bestiary_recover_instance(&b,1,persist,NULL)==CITY_BESTIARY_UNCHANGED);
    assert(b.owned[1].current_hp==28);
    assert(city_bestiary_recover_instance(&b,2,persist,NULL)==CITY_BESTIARY_APPLIED);
    before=b;fail=true;
    assert(city_bestiary_visit(&b,3,2,1,4,persist,NULL)==CITY_BESTIARY_STORAGE_FAILED);
    assert(!memcmp(&before,&b,sizeof(b)));fail=false;
    assert(city_bestiary_visit(&b,3,2,1,4,persist,NULL)==CITY_BESTIARY_APPLIED);
    reload(&b);assert(b.owned[1].friendship==5 && b.owned[0].friendship==0);
    unsigned w=writes;
    assert(city_bestiary_visit(&b,3,2,1,4,persist,NULL)==CITY_BESTIARY_DUPLICATE && w==writes);
    assert(city_bestiary_visit(&b,4,2,1,4,persist,NULL)==CITY_BESTIARY_UNCHANGED);
    reload(&b);assert(b.owned[1].friendship==5); /* same place/reboot does not farm friendship */
    assert(city_bestiary_visit(&b,5,2,2,4,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_visit(&b,6,2,1,4,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(b.owned[1].friendship==12 && b.owned[1].friendship_places==3);
    assert(city_bestiary_visit(&b,7,1,3,4,persist,NULL)==CITY_BESTIARY_INVALID);
    assert(city_bestiary_visit(&b,7,2,0,4,persist,NULL)==CITY_BESTIARY_INVALID);
    assert(city_bestiary_visit(&b,7,2,17,4,persist,NULL)==CITY_BESTIARY_INVALID);
    assert(city_bestiary_capture_personality(&b,6,4,1,&(city_creature_stats_t){39,52,43},3,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(b.owned[1].friendship==12); /* capture alone grants legacy bond only */
    before=b;w=writes;
    assert(city_bestiary_capture_personality(&b,6,4,1,&(city_creature_stats_t){39,52,43},2,persist,NULL)==CITY_BESTIARY_DUPLICATE);
    assert(!memcmp(&before,&b,sizeof(b)) && w==writes);
    b.owned[1].friendship=99;
    assert(city_bestiary_visit(&b,7,2,3,4,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(b.owned[1].friendship==100);
    assert(city_friendship_band(19)==0 && city_friendship_band(20)==1 && city_friendship_band(59)==1 && city_friendship_band(60)==2);
    before=b;fail=true;
    assert(city_bestiary_release_instance(&b,2,persist,NULL)==CITY_BESTIARY_STORAGE_FAILED);
    assert(!memcmp(&before,&b,sizeof(b)));fail=false;
    assert(city_bestiary_release_instance(&b,2,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(!b.buddy_instance_id && !b.buddy_species_id && b.owned[0].instance_id==1);
    reload(&b);assert(!b.buddy_instance_id);
    b.last_visit_sequence=UINT64_MAX;uint64_t seq;assert(!city_bestiary_next_encounter_sequence(&b,&seq));
    /* Full 160-copy v10 fixture: IDs, HP, stats and species bond survive migration. */
    city_bestiary_init(&b);
    for(unsigned i=0;i<CITY_MAX_OWNED_POKEMON;++i)
        assert(city_bestiary_capture(&b,i+1,25,1,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_choose_buddy(&b,25,persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_damage_instance(&b,100,9,persist,NULL)==CITY_BESTIARY_APPLIED);
    b.records[city_species_index(25)].friendship=70;
    assert(city_bestiary_encode(&b,saved));
    enum { OLD_SIZE=40+CITY_SPECIES_COUNT*22+160*16+4 };
    uint8_t old[OLD_SIZE];memset(old,0,sizeof(old));
    memcpy(old,saved,32);old[4]=10;old[5]=0;
    memcpy(old+40,saved+48,CITY_SPECIES_COUNT*22);
    for(unsigned i=0;i<160;++i)memcpy(old+40+CITY_SPECIES_COUNT*22+i*16,saved+48+CITY_SPECIES_COUNT*22+i*CITY_OWNED_POKEMON_BYTES,13);
    checksum(old,sizeof(old));
    before=b;assert(city_bestiary_decode(old,sizeof(old),&b));
    assert(b.owned_count==160 && b.buddy_instance_id==0 && b.buddy_species_id==25);
    assert(b.records[city_species_index(25)].friendship==70);
    for(unsigned i=0;i<160;++i){
        assert(b.owned[i].instance_id==before.owned[i].instance_id);
        assert(!memcmp(&b.owned[i].stats,&before.owned[i].stats,sizeof(stats)));
        assert(b.owned[i].current_hp==before.owned[i].current_hp);
        assert(b.owned[i].personality==i%6 && b.owned[i].friendship==0);
    }
    assert(city_bestiary_encode(&b,saved));reload(&b);
    before=b;saved[4]=13;checksum(saved,sizeof(saved));
    assert(!city_bestiary_decode(saved,sizeof(saved),&b) && !memcmp(&b,&before,sizeof(b)));
    assert(city_bestiary_encode(&b,saved));saved[48+CITY_SPECIES_COUNT*22+13]=6;checksum(saved,sizeof(saved));
    assert(!city_bestiary_decode(saved,sizeof(saved),&b));
    assert(city_bestiary_encode(&b,saved));saved[48+CITY_SPECIES_COUNT*22+14]=101;checksum(saved,sizeof(saved));
    assert(!city_bestiary_decode(saved,sizeof(saved),&b));
    assert(city_bestiary_encode(&b,saved));saved[32]=255;checksum(saved,sizeof(saved));
    assert(!city_bestiary_decode(saved,sizeof(saved),&b));
    printf("Personality identity, rewards, failure, migration passed; model=%zu wire=%u\n",sizeof(b),(unsigned)CITY_BESTIARY_ENCODED_BYTES);
    return 0;
}
