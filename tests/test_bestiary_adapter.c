#include "bsp_bestiary_store.h"
#include "nvs.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint8_t saved[CITY_BESTIARY_ENCODED_BYTES], staged[CITY_BESTIARY_ENCODED_BYTES], legacy[156];
static size_t saved_len, staged_len;
static bool legacy_exists, fail_commit, fail_set, bad_readback;
static unsigned commits;
static bool fail_read_after_commit;
static uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned j = 0; j < 8U; ++j) {
            crc = (crc >> 1U) ^
                (UINT32_C(0xedb88320) &
                 (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return ~crc;
}
static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}
static void write_u32_le(uint8_t *data, uint32_t value)
{
    for (unsigned i = 0; i < 4U; ++i) {
        data[i] = (uint8_t)(value >> (8U * i));
    }
}
static void compact_saved_catalog(uint16_t count)
{
    assert(
        count > 0U && count < CITY_SPECIES_COUNT &&
        saved_len == sizeof(saved));
    uint8_t full[CITY_BESTIARY_ENCODED_BYTES];
    memcpy(full, saved, sizeof(full));
    memset(saved, 0, sizeof(saved));
    memcpy(saved, full, CITY_BESTIARY_HEADER_BYTES);
    write_u16_le(saved + 6U, count);
    memcpy(
        saved + CITY_BESTIARY_HEADER_BYTES,
        full + CITY_BESTIARY_HEADER_BYTES,
        count * CITY_BESTIARY_RECORD_BYTES);
    const size_t old_owned = CITY_BESTIARY_HEADER_BYTES +
        CITY_SPECIES_COUNT * CITY_BESTIARY_RECORD_BYTES;
    const size_t new_owned = CITY_BESTIARY_HEADER_BYTES +
        count * CITY_BESTIARY_RECORD_BYTES;
    memcpy(
        saved + new_owned,
        full + old_owned,
        CITY_MAX_OWNED_POKEMON * CITY_OWNED_POKEMON_BYTES);
    saved_len = new_owned +
        CITY_MAX_OWNED_POKEMON * CITY_OWNED_POKEMON_BYTES + 4U;
    write_u32_le(
        saved + saved_len - 4U, crc32(saved, saved_len - 4U));
}
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
    memcpy(saved,staged,staged_len);saved_len=staged_len;
    if (fail_read_after_commit) bad_readback=true;
    return ESP_OK;
}
static void make_legacy(void)
{
    city_bestiary_t b; assert(city_bestiary_import_legacy_count(&b,20));
    b.records[1].last_place_id=2;
    b.last_settled_sequence=20;b.wild_cooldown_active=true;
    for(unsigned i=0;i<b.owned_count;++i)b.owned[i].place_id=2;
    uint8_t bytes[CITY_BESTIARY_ENCODED_BYTES];assert(city_bestiary_encode(&b,bytes));
    memset(legacy,0,sizeof(legacy));memcpy(legacy,bytes,16);legacy[4]=5;legacy[6]=3;
    for(unsigned i=0;i<3;++i)memcpy(legacy+16+i*20,bytes+CITY_BESTIARY_HEADER_BYTES+i*CITY_BESTIARY_RECORD_BYTES,20);
    legacy[76]=1;
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
    /* Upgrade a current-key v6 save in place, only publishing after commit. */
    uint8_t current[CITY_BESTIARY_ENCODED_BYTES];memcpy(current,saved,saved_len);
    memset(saved,0,sizeof(saved));memcpy(saved,current,32);saved[4]=6;saved[5]=0;
    for(unsigned i=0;i<CITY_SPECIES_COUNT;++i)memcpy(saved+32+i*20,current+CITY_BESTIARY_HEADER_BYTES+i*CITY_BESTIARY_RECORD_BYTES,20);
    saved_len=32U+CITY_SPECIES_COUNT*20U+4U;
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<saved_len-4;++i){crc^=saved[i];for(unsigned j=0;j<8;++j)crc=(crc>>1)^(0xedb88320U&(uint32_t)-(int32_t)(crc&1));}
    crc=~crc;for(unsigned j=0;j<4;++j)saved[saved_len-4+j]=(uint8_t)(crc>>(8*j));
    city_bestiary_t empty;city_bestiary_init(&empty);
    fail_commit=true;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&empty,&migrated)==ESP_FAIL);
    assert(!migrated && empty.records[1].capture_count==0 && saved[4]==6);
    fail_commit=false;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&empty,&migrated)==ESP_OK);
    assert(migrated && saved[4]==CITY_BESTIARY_SCHEMA_VERSION && empty.records[1].capture_count==20 && empty.buddy_species_id==0);
    /* A same-schema save from a smaller catalog upgrades by stable ID. */
    compact_saved_catalog((uint16_t)(CITY_SPECIES_COUNT-1U));
    city_bestiary_init(&empty);
    city_bestiary_t unchanged=empty;
    size_t old_len=saved_len;
    uint8_t old_catalog[CITY_BESTIARY_ENCODED_BYTES];
    memcpy(old_catalog,saved,saved_len);
    fail_set=true;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&empty,&migrated)==ESP_FAIL);
    assert(!migrated && memcmp(&empty,&unchanged,sizeof(empty))==0);
    assert(saved_len==old_len && memcmp(saved,old_catalog,old_len)==0);
    fail_set=false;fail_commit=true;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&empty,&migrated)==ESP_FAIL);
    assert(!migrated && memcmp(&empty,&unchanged,sizeof(empty))==0);
    assert(saved_len==old_len && memcmp(saved,old_catalog,old_len)==0);
    fail_commit=false;fail_read_after_commit=true;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&empty,&migrated)==ESP_FAIL);
    assert(!migrated && memcmp(&empty,&unchanged,sizeof(empty))==0);
    assert(saved_len==sizeof(saved));
    fail_read_after_commit=false;bad_readback=false;
    count=commits;
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&empty,&migrated)==ESP_OK);
    assert(!migrated && commits==count && empty.records[1].capture_count==20);
    compact_saved_catalog((uint16_t)(CITY_SPECIES_COUNT-1U));
    count=commits;city_bestiary_init(&empty);
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&empty,&migrated)==ESP_OK);
    assert(migrated && commits==count+1U && saved_len==sizeof(saved));
    assert(((uint16_t)saved[6]|((uint16_t)saved[7]<<8U))==CITY_SPECIES_COUNT);
    assert(empty.records[CITY_SPECIES_COUNT-1U].state==CITY_DISCOVERY_UNKNOWN);
    count=commits;city_bestiary_init(&empty);
    assert(bsp_bestiary_store_load(&BSP_BESTIARY_STORE_DEFAULT,&empty,&migrated)==ESP_OK);
    assert(!migrated && commits==count);
    puts("Bestiary adapter: failed stages, readback, retry, retained legacy and corruption passed");
    return 0;
}
