#include "bestiary_service.h"
#include <assert.h>
#include <string.h>
static bool save_ok = true;
static unsigned writes;
static bool save(const city_bestiary_t *b, void *ctx) {
    (void)ctx; assert(city_bestiary_is_valid(b)); ++writes; return save_ok;
}
static void checksum(uint8_t *p, size_t n) {
    uint32_t c = UINT32_MAX;
    for (size_t i=0;i<n-4;++i) { c^=p[i]; for(unsigned j=0;j<8;++j) c=(c>>1)^(0xedb88320U & (uint32_t)-(int32_t)(c&1)); }
    c=~c; for(unsigned j=0;j<4;++j) p[n-4+j]=(uint8_t)(c>>(8*j));
}
int main(void) {
    city_bestiary_t b; city_bestiary_init(&b);
    assert(city_bestiary_choose_buddy(&b,1,save,0)==CITY_BESTIARY_INVALID);
    assert(city_bestiary_choose_buddy(NULL,1,save,0)==CITY_BESTIARY_INVALID);
    assert(city_bestiary_capture(&b,1,1,1,save,0)==CITY_BESTIARY_APPLIED);
    assert(b.records[0].friendship==0);
    save_ok=false;
    assert(city_bestiary_choose_buddy(&b,1,save,0)==CITY_BESTIARY_STORAGE_FAILED);
    assert(b.buddy_species_id==0);
    save_ok=true;
    assert(city_bestiary_choose_buddy(&b,1,save,0)==CITY_BESTIARY_APPLIED);
    unsigned previous=writes;
    assert(city_bestiary_choose_buddy(&b,1,save,0)==CITY_BESTIARY_UNCHANGED && writes==previous);
    save_ok=false;
    assert(city_bestiary_capture(&b,2,4,1,save,0)==CITY_BESTIARY_STORAGE_FAILED);
    assert(b.records[0].friendship==0 && b.records[0].buddy_places==0);
    save_ok=true;
    assert(city_bestiary_capture(&b,2,4,1,save,0)==CITY_BESTIARY_APPLIED);
    assert(b.records[0].friendship==3 && b.records[0].buddy_places==1);
    assert(city_bestiary_capture(&b,2,4,1,save,0)==CITY_BESTIARY_DUPLICATE);
    assert(b.records[0].friendship==3);
    assert(city_bestiary_capture(&b,3,4,1,save,0)==CITY_BESTIARY_APPLIED);
    assert(b.records[0].friendship==4);
    assert(city_bestiary_capture(&b,4,4,16,save,0)==CITY_BESTIARY_APPLIED);
    assert(b.records[0].friendship==7 && b.records[0].buddy_places==0x8001);
    assert(city_bestiary_capture(&b,5,7,0,save,0)==CITY_BESTIARY_APPLIED);
    assert(b.records[0].friendship==8);
    assert(city_bestiary_choose_buddy(&b,4,save,0)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_capture(&b,6,1,1,save,0)==CITY_BESTIARY_APPLIED);
    assert(b.records[0].friendship==8 && b.records[1].friendship==3);
    assert(city_bestiary_choose_buddy(&b,1,save,0)==CITY_BESTIARY_APPLIED);
    uint8_t bytes[CITY_BESTIARY_ENCODED_BYTES];
    assert(city_bestiary_encode(&b,bytes));
    city_bestiary_t restored; assert(city_bestiary_decode(bytes,sizeof(bytes),&restored));
    assert(memcmp(&b,&restored,sizeof(b))==0);
    assert(city_bestiary_capture(&restored,6,1,1,save,0)==CITY_BESTIARY_DUPLICATE);
    for(unsigned i=7;i<130;++i) assert(city_bestiary_capture(&b,i,1,1,save,0)==CITY_BESTIARY_APPLIED);
    assert(b.records[0].friendship==100);
    /* Actual v6 layout has 20-byte records; old captures must not earn retroactive points. */
    uint8_t old[32U + CITY_SPECIES_COUNT * 20U + 4U] = {0};
    memcpy(old,bytes,32); old[4]=6; old[24]=old[25]=0;
    for(unsigned i=0;i<CITY_SPECIES_COUNT;++i) memcpy(old+32+i*20,bytes+CITY_BESTIARY_HEADER_BYTES+i*CITY_BESTIARY_RECORD_BYTES,16);
    checksum(old,sizeof(old));
    assert(city_bestiary_decode(old,sizeof(old),&restored));
    assert(restored.buddy_species_id==0 && restored.last_settled_sequence==6);
    for(unsigned i=0;i<CITY_SPECIES_COUNT;++i) assert(restored.records[i].friendship==0 && restored.records[i].buddy_places==0);
    /* Valid CRC is insufficient for an invalid buddy or friendship. */
    assert(city_bestiary_encode(&b,bytes)); bytes[24]=25; checksum(bytes,sizeof(bytes));
    assert(!city_bestiary_decode(bytes,sizeof(bytes),&restored));
    assert(city_bestiary_encode(&b,bytes)); bytes[CITY_BESTIARY_HEADER_BYTES+16]=101; checksum(bytes,sizeof(bytes));
    assert(!city_bestiary_decode(bytes,sizeof(bytes),&restored));
    return 0;
}
