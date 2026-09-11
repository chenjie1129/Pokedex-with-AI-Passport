#include "bestiary_service.h"
#include "encounter_selector.h"
#include "passport_progress.h"
#include <assert.h>
#include <string.h>
static bool ok=true;
static unsigned writes;
static bool save(const city_bestiary_t *b, void *ctx) { (void)ctx; ++writes; assert(city_bestiary_is_valid(b)); return ok; }
static void crc(uint8_t *p,size_t n) {
 uint32_t c=UINT32_MAX;for(size_t i=0;i<n-4;++i){c^=p[i];for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320U&(uint32_t)-(int32_t)(c&1));}
 c=~c;for(unsigned i=0;i<4;++i)p[n-4+i]=(uint8_t)(c>>(8*i));
}
static void encode_v7(const city_bestiary_t *b, uint8_t old[276]) {
 uint8_t current[CITY_BESTIARY_ENCODED_BYTES];assert(city_bestiary_encode(b,current));
 memset(old,0,276);memcpy(old,current,32);old[4]=7;old[5]=0;old[6]=12;old[7]=0;
 for(unsigned i=0;i<12;++i)memcpy(old+32+i*20,current+CITY_BESTIARY_HEADER_BYTES+i*CITY_BESTIARY_RECORD_BYTES,20);
 crc(old,276);
}
static void eligible(city_bestiary_t *b,uint16_t id) {
 city_bestiary_init(b);
 assert(city_bestiary_capture(b,1,id,1,save,NULL)==CITY_BESTIARY_APPLIED);
 assert(city_bestiary_choose_buddy(b,id,save,NULL)==CITY_BESTIARY_APPLIED);
 for(unsigned i=2;i<=25;++i) assert(city_bestiary_capture(b,i,id,i<=4?i-1:1,save,NULL)==CITY_BESTIARY_APPLIED);
 assert(city_evolution_ready(b,id));
}
int main(void) {
 const uint16_t roots[]={1,4,7},targets[]={2,5,8};
 assert(!city_evolution_ready(NULL,1));assert(city_evolution_target(25)==0);
 for(unsigned i=0;i<3;++i) {
  city_bestiary_t b;eligible(&b,roots[i]);
  assert(city_evolution_target(roots[i])==targets[i]);
  city_creature_record_t *r=city_bestiary_record(&b,roots[i]);
  assert(r->friendship==30 && city_buddy_place_count(r)==3);
  r->friendship=29;assert(!city_evolution_ready(&b,roots[i]));r->friendship=30;
  r->buddy_places=3;assert(!city_evolution_ready(&b,roots[i]));r->buddy_places=7;
  b.buddy_species_id=0;assert(!city_evolution_ready(&b,roots[i]));b.buddy_species_id=roots[i];
  city_bestiary_t before=b;ok=false;
  assert(city_bestiary_evolve(&b,roots[i],save,NULL)==CITY_BESTIARY_STORAGE_FAILED);
  assert(memcmp(&b,&before,sizeof(b))==0);ok=true;
  assert(city_bestiary_evolve(&b,roots[i],save,NULL)==CITY_BESTIARY_APPLIED);
  assert(b.last_settled_sequence==25 && b.buddy_species_id==targets[i]);
  assert(memcmp(city_bestiary_record(&b,roots[i]),city_bestiary_record(&before,roots[i]),sizeof(*r))==0);
  const city_creature_record_t *t=city_bestiary_record(&b,targets[i]);
  assert(t->state==CITY_DISCOVERY_CAPTURED && t->capture_count==0 && t->evolution_obtained);
  assert(t->friendship==30 && t->buddy_places==7 && city_bestiary_captured_count(&b)==2);
  unsigned count=writes;assert(city_bestiary_evolve(&b,roots[i],save,NULL)==CITY_BESTIARY_UNCHANGED && writes==count);
  uint8_t bytes[CITY_BESTIARY_ENCODED_BYTES];assert(city_bestiary_encode(&b,bytes));
  city_bestiary_t restored;assert(city_bestiary_decode(bytes,sizeof(bytes),&restored));assert(memcmp(&b,&restored,sizeof(b))==0);
  assert(city_bestiary_capture(&restored,25,roots[i],1,save,NULL)==CITY_BESTIARY_DUPLICATE);
  assert(city_bestiary_choose_buddy(&restored,roots[i],save,NULL)==CITY_BESTIARY_APPLIED);
  assert(!city_evolution_ready(&restored,roots[i]));
  /* Upgrade the actual 12-species schema 7 envelope, preserving buddy data. */
  uint8_t old[276];encode_v7(&before,old);
  assert(city_bestiary_decode(old,sizeof(old),&restored));
  assert(restored.buddy_species_id==roots[i] && city_evolution_ready(&restored,roots[i]));
  assert(memcmp(restored.records,before.records,12*sizeof(*r))==0);
  assert(city_bestiary_record(&restored,targets[i])->state==CITY_DISCOVERY_UNKNOWN);
  assert(city_bestiary_encode(&b,bytes));bytes[32+city_species_index(targets[i])*CITY_BESTIARY_RECORD_BYTES+3]=2;crc(bytes,sizeof(bytes));
  assert(!city_bestiary_decode(bytes,sizeof(bytes),&restored));
 }
 city_bestiary_t b;city_bestiary_init(&b);
 for(unsigned i=0;i<12;++i)assert(city_bestiary_capture(&b,i+1,city_species_id_at(i),1,save,NULL)==CITY_BESTIARY_APPLIED);
 uint8_t current[CITY_BESTIARY_ENCODED_BYTES];assert(city_bestiary_encode(&b,current));
 uint8_t old9[32U+CITY_SPECIES_COUNT*22U+4U]={0};
 memcpy(old9,current,26);old9[4]=9;old9[5]=0;
 for(unsigned i=0;i<CITY_SPECIES_COUNT;++i)
  memcpy(old9+32+i*22,current+CITY_BESTIARY_HEADER_BYTES+i*CITY_BESTIARY_RECORD_BYTES,22);
 crc(old9,sizeof(old9));
 city_bestiary_t migrated9;assert(city_bestiary_decode(old9,sizeof(old9),&migrated9));
 assert(migrated9.owned_count==12 && migrated9.next_instance_id==13);
 for(unsigned i=0;i<migrated9.owned_count;++i)assert(migrated9.owned[i].migrated);
 for(unsigned seed=0;seed<2000;++seed){city_encounter_selection_t e;assert(city_encounter_select(3,true,&b,seed,&e));assert(city_species_definition(e.species_id)->evolves_from==0);assert(city_wild_encounter_select(seed,&e));assert(city_species_definition(e.species_id)->evolves_from==0);}
 city_passport_stamps_t stamps={.count=3,.place_ids={1,2,3}};
 city_passport_progress_t p=city_passport_progress(&stamps,&b);
 assert(p.goal==CITY_PASSPORT_EVOLVE_SPECIES && p.target==1);
 return 0;
}
