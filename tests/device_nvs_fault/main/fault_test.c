#include "bestiary_service.h"
#include "bsp_bestiary_store.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "esp_flash.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define GUARD_ADDRESS 0x309000U
#define SCRATCH_ADDRESS 0x30a000U
#define SCRATCH_SIZE 0x6000U
#define MAGIC 0x46543231U
static const char *TAG="nvs_fault";
static esp_partition_t scratch;
static nvs_handle_t control;
static nvs_handle_t adapter_handle;
static unsigned fault;
static bool committed;
static unsigned hits;
static city_bestiary_t *model, *expected, *unchanged;
static uint8_t *encoded, *other;
static const bsp_bestiary_store_t store={
    .namespace_name="fault_test", .blob_key="snapshot", .legacy_count_key="old_count"
};
static void stop(const char *reason)
{
    ESP_LOGE(TAG,"FAULT_FAIL reason=%s",reason);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
#define REQUIRE(x) do { if (!(x)) stop(#x); } while (0)

/* These symbols replace NVS calls only in the separately compiled adapter. */
esp_err_t fault_nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *h)
{
    if (strcmp(name,"fault_test")) return ESP_ERR_INVALID_ARG;
    esp_err_t e=nvs_open_from_partition("fault_scratch",name,mode,h);
    if (e==ESP_OK) adapter_handle=*h;
    return e;
}
esp_err_t fault_nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t len)
{
    REQUIRE(h==adapter_handle && !strcmp(key,"snapshot"));
    if (fault==1) { ++hits; return ESP_FAIL; }
    esp_err_t e=nvs_set_blob(h,key,data,len);
    if (e==ESP_OK && fault==2) { ++hits; return ESP_FAIL; }
    return e;
}
esp_err_t fault_nvs_commit(nvs_handle_t h)
{
    REQUIRE(h==adapter_handle);
    if (fault==3) { ++hits; return ESP_FAIL; }
    esp_err_t e=nvs_commit(h);
    committed=e==ESP_OK;
    if (committed && fault==5) {
        ESP_LOGI(TAG,"FAULT_RESTART point=after_commit_before_publication");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }
    return e;
}
esp_err_t fault_nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *len)
{
    if (fault==4 && committed) { ++hits; return ESP_FAIL; }
    return nvs_get_blob(h,key,out,len);
}
static uint32_t crc32(const uint8_t *data,size_t size)
{
    uint32_t c=UINT32_MAX;
    for(size_t i=0;i<size;++i) { c^=data[i]; for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320U & (uint32_t)-(int32_t)(c&1)); }
    return ~c;
}
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i)); }
static bool same(const city_bestiary_t *a,const city_bestiary_t *b)
{
    return city_bestiary_encode(a,encoded) && city_bestiary_encode(b,other) &&
        memcmp(encoded,other,CITY_BESTIARY_ENCODED_BYTES)==0;
}
static nvs_handle_t open_ns(const char *name)
{
    nvs_handle_t h; REQUIRE(nvs_open_from_partition("fault_scratch",name,NVS_READWRITE,&h)==ESP_OK); return h;
}
static void seed_old(void)
{
    REQUIRE(CITY_SPECIES_COUNT==16 && CITY_BESTIARY_SCHEMA_VERSION==12);
    fault=0;committed=false;hits=0;
    REQUIRE(city_bestiary_import_legacy_count(expected,2));
    REQUIRE(city_bestiary_encode(expected,encoded));
    const size_t at=CITY_BESTIARY_HEADER_BYTES+15*CITY_BESTIARY_RECORD_BYTES;
    memmove(encoded+at,encoded+at+CITY_BESTIARY_RECORD_BYTES,
        CITY_MAX_OWNED_POKEMON*CITY_OWNED_POKEMON_BYTES);
    const size_t len=CITY_BESTIARY_ENCODED_BYTES-CITY_BESTIARY_RECORD_BYTES;
    encoded[6]=15;encoded[7]=0;put32(encoded+20,5);put32(encoded+len-4,crc32(encoded,len-4));
    nvs_handle_t h=open_ns("fault_test");
    REQUIRE(nvs_erase_all(h)==ESP_OK);
    REQUIRE(nvs_set_blob(h,"snapshot",encoded,len)==ESP_OK);
    REQUIRE(nvs_commit(h)==ESP_OK);nvs_close(h);
    city_bestiary_init(model);*unchanged=*model;
}
static void reopen_scratch(void)
{
    nvs_close(control);
    REQUIRE(nvs_flash_deinit_partition("fault_scratch")==ESP_OK);
    REQUIRE(nvs_flash_init_partition_ptr(&scratch)==ESP_OK);
    control=open_ns("control");
}
static void test_migration_faults(void)
{
    const char *names[]={"","before_set","after_set_before_commit","commit_error","readback_error"};
    for(unsigned i=1;i<=4;++i) {
        seed_old();fault=i;
        bool migrated=true;
        REQUIRE(bsp_bestiary_store_load(&store,model,&migrated)==ESP_FAIL);
        REQUIRE(hits==1 && !migrated && memcmp(model,unchanged,sizeof(*model))==0);
        fault=0;committed=false;
        reopen_scratch();
        REQUIRE(bsp_bestiary_store_load(&store,model,&migrated)==ESP_OK);
        REQUIRE(same(model,expected));
        REQUIRE(bsp_bestiary_store_load(&store,model,&migrated)==ESP_OK && !migrated);
        ESP_LOGI(TAG,"FAULT_CASE_PASS case=%s caller=unchanged recovery=complete",names[i]);
    }
    seed_old();nvs_handle_t h=open_ns("fault_test");size_t len=CITY_BESTIARY_ENCODED_BYTES;
    REQUIRE(nvs_get_blob(h,"snapshot",encoded,&len)==ESP_OK);encoded[48]^=1;
    REQUIRE(nvs_set_blob(h,"snapshot",encoded,len)==ESP_OK);
    REQUIRE(nvs_set_u32(h,"old_count",99)==ESP_OK);REQUIRE(nvs_commit(h)==ESP_OK);nvs_close(h);
    bool migrated=true;
    REQUIRE(bsp_bestiary_store_load(&store,model,&migrated)==ESP_ERR_INVALID_STATE);
    REQUIRE(!migrated && memcmp(model,unchanged,sizeof(*model))==0);
    ESP_LOGI(TAG,"FAULT_CASE_PASS case=corrupt_current no_legacy_fallback=1");
}
static void test_full(void)
{
    seed_old();REQUIRE(bsp_bestiary_store_load(&store,model,NULL)==ESP_OK);
    for(unsigned i=0;i<64;++i) REQUIRE(city_bestiary_capture(model,3+i,CITY_SPECIES_CHARMANDER,1,
        bsp_bestiary_store_persist,(void*)&store)==CITY_BESTIARY_APPLIED);
    const uint64_t next_sequence=model->last_settled_sequence+1;
    ESP_LOGI(TAG,"FAULT_CASE_PASS case=repeated_real_nvs_writes cycles=64");
    nvs_handle_t filler=open_ns("filler");
    esp_err_t e=ESP_OK;unsigned i;
    memset(other,0xa5,256);
    for(i=0;i<512 && e==ESP_OK;++i) { char key[16];snprintf(key,sizeof(key),"b%u",i);e=nvs_set_blob(filler,key,other,256); }
    REQUIRE(e==ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    /* Consume remaining small entries so the next actual save must hit full. */
    e=ESP_OK;
    for(i=0;i<1024 && e==ESP_OK;++i) { char key[16];snprintf(key,sizeof(key),"s%u",i);e=nvs_set_u32(filler,key,i); }
    REQUIRE(e==ESP_ERR_NVS_NOT_ENOUGH_SPACE);REQUIRE(nvs_commit(filler)==ESP_OK);
    *unchanged=*model;
    REQUIRE(city_bestiary_capture(model,next_sequence,CITY_SPECIES_CHARMANDER,1,
        bsp_bestiary_store_persist,(void*)&store)==CITY_BESTIARY_STORAGE_FAILED);
    REQUIRE(memcmp(model,unchanged,sizeof(*model))==0);
    REQUIRE(bsp_bestiary_store_load(&store,model,NULL)==ESP_OK && same(model,unchanged));
    REQUIRE(nvs_erase_all(filler)==ESP_OK);REQUIRE(nvs_commit(filler)==ESP_OK);nvs_close(filler);
    reopen_scratch();
    REQUIRE(city_bestiary_capture(model,next_sequence,CITY_SPECIES_CHARMANDER,1,
        bsp_bestiary_store_persist,(void*)&store)==CITY_BESTIARY_APPLIED);
    REQUIRE(bsp_bestiary_store_load(&store,expected,NULL)==ESP_OK && same(model,expected));
    ESP_LOGI(TAG,"FAULT_CASE_PASS case=real_nvs_full caller=unchanged cleanup_retry=pass");
}
static void init_scratch(void)
{
    const esp_partition_t *app=esp_ota_get_running_partition();
    REQUIRE(app && app->address==0x10000 && app->size==0x300000 && app->subtype==ESP_PARTITION_SUBTYPE_APP_FACTORY);
    uint32_t size=0;REQUIRE(esp_flash_get_size(NULL,&size)==ESP_OK && size==0x800000);
    esp_image_metadata_t image={0};esp_partition_pos_t pos={.offset=app->address,.size=app->size};
    REQUIRE(esp_image_verify(ESP_IMAGE_VERIFY,&pos,&image)==ESP_OK);
    REQUIRE(image.image_len<=GUARD_ADDRESS-app->address);
    scratch=*app;scratch.address=SCRATCH_ADDRESS;scratch.size=SCRATCH_SIZE;
    scratch.type=ESP_PARTITION_TYPE_DATA;scratch.subtype=ESP_PARTITION_SUBTYPE_DATA_NVS;
    strlcpy(scratch.label,"fault_scratch",sizeof(scratch.label));
    /* Validate a dedicated guard sector BEFORE NVS may inspect or modify pages. */
    bool blank=true;
    for(size_t at=GUARD_ADDRESS;at<SCRATCH_ADDRESS+SCRATCH_SIZE;at+=256) {
        REQUIRE(esp_partition_read(app,at-app->address,encoded,256)==ESP_OK);
        for(unsigned j=0;j<256;++j) if(encoded[j]!=0xff)blank=false;
    }
    const uint32_t guard[4]={MAGIC,GUARD_ADDRESS,SCRATCH_SIZE,~MAGIC};
    if(blank) REQUIRE(esp_partition_write(app,GUARD_ADDRESS-app->address,guard,sizeof(guard))==ESP_OK);
    else {
        REQUIRE(esp_partition_read(app,GUARD_ADDRESS-app->address,encoded,sizeof(guard))==ESP_OK);
        REQUIRE(memcmp(encoded,guard,sizeof(guard))==0);
    }
    REQUIRE(nvs_flash_init_partition_ptr(&scratch)==ESP_OK);
    control=open_ns("control");uint32_t magic=0;
    if(blank) { REQUIRE(nvs_set_u32(control,"magic",MAGIC)==ESP_OK);REQUIRE(nvs_commit(control)==ESP_OK); }
    else REQUIRE(nvs_get_u32(control,"magic",&magic)==ESP_OK && magic==MAGIC);
    ESP_LOGI(TAG,"FAULT_START scratch=0x30a000 bytes=24576 player_nvs=untouched");
}
void app_main(void)
{
    model=calloc(1,sizeof(*model));expected=calloc(1,sizeof(*expected));unchanged=calloc(1,sizeof(*unchanged));
    encoded=malloc(CITY_BESTIARY_ENCODED_BYTES);other=malloc(CITY_BESTIARY_ENCODED_BYTES);
    REQUIRE(model && expected && unchanged && encoded && other);
    vTaskDelay(pdMS_TO_TICKS(5000));
    init_scratch();uint32_t stage=0;esp_err_t e=nvs_get_u32(control,"stage",&stage);
    REQUIRE(e==ESP_OK || e==ESP_ERR_NVS_NOT_FOUND);
    if(stage==0) {
        test_migration_faults();test_full();seed_old();
        REQUIRE(bsp_bestiary_store_load(&store,model,NULL)==ESP_OK);
        REQUIRE(nvs_set_u32(control,"stage",1)==ESP_OK);REQUIRE(nvs_commit(control)==ESP_OK);
        fault=5;
        (void)city_bestiary_capture(model,3,CITY_SPECIES_CHARMANDER,1,bsp_bestiary_store_persist,(void*)&store);
        stop("restart_not_reached");
    }
    REQUIRE(stage==1);bool migrated=true;
    REQUIRE(bsp_bestiary_store_load(&store,model,&migrated)==ESP_OK && !migrated);
    REQUIRE(model->owned_count==3 && model->last_settled_sequence==3 && model->records[1].capture_count==3);
    REQUIRE(city_bestiary_capture(model,3,CITY_SPECIES_CHARMANDER,1,bsp_bestiary_store_persist,(void*)&store)==CITY_BESTIARY_DUPLICATE);
    ESP_LOGI(TAG,"FAULT_CASE_PASS case=restart_after_commit exactly_once=1");
    nvs_close(control);REQUIRE(nvs_flash_deinit_partition("fault_scratch")==ESP_OK);
    const esp_partition_t *app=esp_ota_get_running_partition();
    REQUIRE(esp_partition_erase_range(app,GUARD_ADDRESS-app->address,0x7000)==ESP_OK);
    ESP_LOGI(TAG,"FAULT_SUITE_PASS cases=8 scratch=erased power_cut=not_tested heap_min=%lu",(unsigned long)esp_get_minimum_free_heap_size());
    for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
}
