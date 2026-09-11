#include "bsp_bestiary_store.h"
#include "nvs.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint8_t saved[CITY_BESTIARY_ENCODED_BYTES], staged[CITY_BESTIARY_ENCODED_BYTES], legacy[156];
static size_t saved_len, staged_len;
static bool legacy_exists, fail_commit, fail_set, bad_readback;
static unsigned commits;
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h)
{ (void)name; (void)mode; *h=1; staged_len=0; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; staged_len=0; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    (void)h;
    const bool old = strcmp(key,"bestiary_004")==0;
    size_t size=old ? (legacy_exists ? sizeof(legacy) : 0) : saved_len;
    if (!size) return ESP_ERR_NVS_NOT_FOUND;
    if (!old && bad_readback) return ESP_FAIL;
    if (out) { if (*len < size) return ESP_ERR_INVALID_SIZE; memcpy(out,old?legacy:saved,size); }
    *len=size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t len)
{
    (void)h;
    assert(strcmp(key,"bestiary_v6")==0 && len<=sizeof(staged));
    if (fail_set) return ESP_FAIL;
    memcpy(staged,data,len); staged_len=len; return ESP_OK;
}
esp_err_t nvs_get_u32(nvs_handle_t h,const char *key,uint32_t *out)
{ (void)h;(void)key;(void)out;return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h; ++commits;
    if (fail_commit) return ESP_FAIL;
    memcpy(saved,staged,staged_len);saved_len=staged_len;return ESP_OK;
}
static void make_legacy(void)
{
    city_bestiary_t b; city_bestiary_init(&b);
    b.records[1].state=CITY_DISCOVERY_CAPTURED;b.records[1].capture_count=20;
    b.records[1].last_place_id=2;
    b.records[1].latest_stats=(city_creature_stats_t){39,52,43};
    b.records[1].best_stats=b.records[1].latest_stats;
    b.last_settled_sequence=20;b.wild_cooldown_active=true;
    uint8_t bytes[CITY_BESTIARY_ENCODED_BYTES];assert(city_bestiary_encode(&b,bytes));
    memset(legacy,0,sizeof(legacy));memcpy(legacy,bytes,16);legacy[4]=5;legacy[6]=3;
    memcpy(legacy+16,bytes+32,60);legacy[76]=1;
    uint32_t c=UINT32_MAX;
    for(unsigned i=0;i<152;++i){c^=legacy[i];for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320U&(uint32_t)-(int32_t)(c&1));}
    c=~c;for(unsigned i=0;i<4;++i)legacy[152+i]=(uint8_t)(c>>(i*8));
    legacy_exists=true;
}
int main(void)
{
    make_legacy();uint8_t old[156];memcpy(old,legacy,156);
    city_bestiary_t b;city_bestiary_init(&b);bool migrated;
    fail_set=true;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&b,&migrated)==ESP_FAIL);
    assert(!migrated && saved_len==0 && b.records[1].capture_count==0);
    fail_set=false;fail_commit=true;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&b,&migrated)==ESP_FAIL);
    assert(saved_len==0 && b.records[1].capture_count==0);
    fail_commit=false;bad_readback=true;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&b,&migrated)==ESP_FAIL);
    assert(saved_len>0 && b.records[1].capture_count==0); // Commit happened, but UI must not publish unverified migration.
    bad_readback=false;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&b,&migrated)==ESP_OK);
    assert(!migrated && b.records[1].capture_count==20 && b.wild_cooldown_active && b.last_settled_sequence==20);
    unsigned count=commits;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&b,&migrated)==ESP_OK && commits==count);
    assert(memcmp(old,legacy,156)==0);
    saved[40]^=1;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&b,&migrated)==ESP_ERR_INVALID_STATE); // Do not revert to legacy.
    saved_len=0;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&b,&migrated)==ESP_OK && migrated);
    fail_commit=true;
    assert(city_bestiary_capture(&b,21,CITY_SPECIES_PIKACHU,1,bsp_bestiary_store_persist,(void*)&BSP_BESTIARY_STORE_DEFAULT)==CITY_BESTIARY_STORAGE_FAILED);
    assert(b.last_settled_sequence==20 && b.records[3].capture_count==0);
    fail_commit=false;
    assert(city_bestiary_capture(&b,21,CITY_SPECIES_PIKACHU,1,bsp_bestiary_store_persist,(void*)&BSP_BESTIARY_STORE_DEFAULT)==CITY_BESTIARY_APPLIED);
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&b,&migrated)==ESP_OK);
    assert(b.records[3].capture_count==1 && b.records[1].capture_count==20 && b.wild_cooldown_active);
    puts("Bestiary adapter: failed stages, readback, retry, retained legacy and corruption passed");
    return 0;
}
