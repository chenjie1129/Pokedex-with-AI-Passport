/* No-save Stage 3 hardware acceptance workload. Diagnostic firmware disables
 * buttons/timers, restores RAM at end and never calls the NVS persist adapter. */
static bool content_smoke_persist(const city_bestiary_t *model,void *unused)
{
    (void)unused;
    uint8_t *wire=malloc(CITY_BESTIARY_STORAGE_MAX_BYTES),*again=malloc(CITY_BESTIARY_STORAGE_MAX_BYTES);
    city_bestiary_t *decoded=malloc(sizeof(*decoded));size_t n=0,m=0;
    bool ok=wire && again && decoded &&
        city_bestiary_encode_sparse(model,wire,CITY_BESTIARY_STORAGE_MAX_BYTES,&n) &&
        city_bestiary_decode(wire,n,decoded) &&
        city_bestiary_encode_sparse(decoded,again,CITY_BESTIARY_STORAGE_MAX_BYTES,&m) && n==m && !memcmp(wire,again,n);
    free(wire);free(again);free(decoded);return ok;
}
static void content_smoke_task(void *unused)
{
    (void)unused;
    city_bestiary_t *original=malloc(sizeof(*original));
    bool ok=original && pokemon_content_active() && bsp_lvgl_lock(2000);
    if (!ok) { ESP_LOGE(TAG,"CONTENT_SMOKE_FAIL setup");free(original);vTaskDelete(NULL);return; }
    *original=s_bestiary;s_save_in_progress=true;
    ok=pokemon_content_verify_cache();
    uint16_t package_id=0;
    for (uint32_t i=0;i<city_species_count();++i) {
        uint16_t id=city_species_runtime_id(i);
        if (!city_species_definition(id)) { package_id=id;break; }
    }
    if (package_id && ok) {
        city_bestiary_init(&s_bestiary);
        ok=city_bestiary_capture(&s_bestiary,1,package_id,1,content_smoke_persist,NULL)==CITY_BESTIARY_APPLIED &&
            city_bestiary_capture(&s_bestiary,2,package_id,2,content_smoke_persist,NULL)==CITY_BESTIARY_APPLIED &&
            city_bestiary_choose_buddy_instance(&s_bestiary,2,content_smoke_persist,NULL)==CITY_BESTIARY_APPLIED &&
            city_bestiary_damage_instance(&s_bestiary,2,5,content_smoke_persist,NULL)==CITY_BESTIARY_APPLIED &&
            city_bestiary_visit(&s_bestiary,3,2,2,package_id,content_smoke_persist,NULL)==CITY_BESTIARY_APPLIED;
    }
    ESP_LOGI(TAG,"CONTENT_SMOKE_CHECK cache=%u package_id=%u model_bytes=%u heap=%lu stack=%u",ok,package_id,
        (unsigned)sizeof(s_bestiary),(unsigned long)esp_get_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(NULL));
    bsp_lvgl_unlock();
    const uint64_t started=now_ms(); uint32_t frames=0;size_t steady_heap=0;
    while (ok && now_ms()-started<CITY_CONTENT_SMOKE_MS) {
        if (!bsp_lvgl_lock(2000)) { ok=false;break; }
        s_bestiary_selection=(frames%((city_species_count()+3)/4))*4;
        if (package_id && frames%32==0) {
            s_bestiary_selection=city_species_position(package_id);set_state(UI_BESTIARY_DETAIL);
        } else set_state(UI_BESTIARY_LIST);
        if (frames%64==0) {
            uint32_t turn=frames/64;
            s_current_species_id=city_species_runtime_id(turn%city_species_count());
            s_current_stats=(city_creature_stats_t){45,40,50};
            set_state(turn%2 ? UI_CAPTURED:UI_ENCOUNTER);
        }
        bsp_lvgl_unlock();
        vTaskDelay(pdMS_TO_TICKS(200));++frames;
        if (frames==320) steady_heap=esp_get_free_heap_size();
        if (frames%300==0) ESP_LOGI(TAG,"CONTENT_SOAK frames=%lu elapsed_ms=%llu heap=%lu min=%lu stack=%u",
            (unsigned long)frames,(unsigned long long)(now_ms()-started),(unsigned long)esp_get_free_heap_size(),
            (unsigned long)esp_get_minimum_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    pokemon_audio_stop();vTaskDelay(pdMS_TO_TICKS(200));
    size_t end_heap=esp_get_free_heap_size();
    if (steady_heap && end_heap+2048<steady_heap) ok=false;
    if (bsp_lvgl_lock(2000)) { s_bestiary=*original;s_save_in_progress=false;set_state(UI_HOME);bsp_lvgl_unlock(); }
    else ok=false;
    free(original);
    ESP_LOGI(TAG,"CONTENT_SMOKE_%s frames=%lu elapsed_ms=%llu heap_start=%lu heap_end=%lu",ok?"PASS":"FAIL",
        (unsigned long)frames,(unsigned long long)(now_ms()-started),(unsigned long)steady_heap,(unsigned long)end_heap);
    vTaskDelete(NULL);
}
