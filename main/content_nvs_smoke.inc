/* Isolated scratch namespace only. Two boots verify real NVS durability for a
 * package-only owned instance; cleanup never opens the player's namespace. */
#include "nvs.h"
#include "esp_system.h"
static const bsp_bestiary_store_t content_test_store={
    .namespace_name="city_pack_tst",.blob_key="snapshot",.legacy_count_key="old_count"};
static bool content_test_no_write(const city_bestiary_t *next,void *context)
{ (void)context;return city_bestiary_is_valid(next); }
static bool content_test_fixture(city_bestiary_t *model,city_bestiary_persist_fn persist,void *context)
{
    city_bestiary_init(model);
    city_creature_stats_t stats={50,44,55};
    return city_bestiary_capture(model,1,60001,1,persist,context)==CITY_BESTIARY_APPLIED &&
        city_bestiary_capture_personality(model,2,60001,2,&stats,CITY_PERSONALITY_PLAYFUL,persist,context)==CITY_BESTIARY_APPLIED &&
        city_bestiary_choose_buddy_instance(model,2,persist,context)==CITY_BESTIARY_APPLIED &&
        city_bestiary_damage_instance(model,2,7,persist,context)==CITY_BESTIARY_APPLIED &&
        city_bestiary_visit(model,3,2,2,60001,persist,context)==CITY_BESTIARY_APPLIED;
}
static void content_nvs_smoke(void)
{
    nvs_handle_t handle;
    if (!pokemon_content_active() || nvs_open(content_test_store.namespace_name,NVS_READWRITE,&handle)!=ESP_OK) {
        ESP_LOGE(TAG,"CONTENT_NVS_FAIL open");return;
    }
    uint32_t phase=0;esp_err_t err=nvs_get_u32(handle,"phase",&phase);
    city_bestiary_t *model=malloc(sizeof(*model)),*expected=malloc(sizeof(*expected));
    bool ok=model && expected && (err==ESP_OK || err==ESP_ERR_NVS_NOT_FOUND);
    if (ok && phase==0) {
        ok=content_test_fixture(model,bsp_bestiary_store_persist,(void *)&content_test_store) &&
            nvs_set_u32(handle,"phase",1)==ESP_OK && nvs_commit(handle)==ESP_OK;
        nvs_close(handle);free(model);free(expected);
        if (ok) { ESP_LOGI(TAG,"CONTENT_NVS_COMMITTED copies=2 rebooting=1");esp_restart(); }
        ESP_LOGE(TAG,"CONTENT_NVS_FAIL commit");return;
    }
    bool migrated=true;
    uint8_t *a=malloc(CITY_BESTIARY_STORAGE_MAX_BYTES),*b=malloc(CITY_BESTIARY_STORAGE_MAX_BYTES);size_t n=0,m=0;
    ok=ok && phase==1 && a && b && bsp_bestiary_store_load(&content_test_store,model,&migrated)==ESP_OK && !migrated &&
        content_test_fixture(expected,content_test_no_write,NULL) &&
        city_bestiary_encode_sparse(model,a,CITY_BESTIARY_STORAGE_MAX_BYTES,&n) &&
        city_bestiary_encode_sparse(expected,b,CITY_BESTIARY_STORAGE_MAX_BYTES,&m) && n==m && !memcmp(a,b,n);
    bool cleaned=nvs_erase_all(handle)==ESP_OK && nvs_commit(handle)==ESP_OK;
    nvs_close(handle);free(a);free(b);free(model);free(expected);
    ESP_LOGI(TAG,"CONTENT_NVS_%s reboot=1 copies=2 buddy=2 cleanup=%u player_owned=%u",ok && cleaned?"PASS":"FAIL",cleaned,s_bestiary.owned_count);
}
