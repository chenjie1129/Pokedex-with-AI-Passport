/* Opt-in, no-save hardware diagnostic. Real user data is restored in RAM.
 * Boot's normal schema migration is the only collection write in this build.
 */
static bool personality_smoke_persist(const city_bestiary_t *next, void *context)
{
    (void)context;
    uint8_t *wire = malloc(CITY_BESTIARY_ENCODED_BYTES);
    city_bestiary_t *decoded = malloc(sizeof(*decoded));
    const bool ok = wire && decoded && city_bestiary_encode(next, wire) &&
        city_bestiary_decode(wire, CITY_BESTIARY_ENCODED_BYTES, decoded) &&
        memcmp(next, decoded, sizeof(*next)) == 0;
    free(decoded); free(wire);
    return ok;
}

static void personality_smoke_task(void *argument)
{
    (void)argument;
    city_bestiary_t *original = malloc(sizeof(*original));
    if (!original || !bsp_lvgl_lock(2000)) { free(original); vTaskDelete(NULL); return; }
    *original = s_bestiary;
    const city_language_t language = s_settings.language;
    city_bestiary_init(&s_bestiary);
    bool ok = true;
    const city_creature_stats_t stats = {35,55,40};
    for (unsigned i=0;i<2;++i)
        ok &= city_bestiary_capture_personality(&s_bestiary,i+1,25,1,&stats,i,
            personality_smoke_persist,NULL)==CITY_BESTIARY_APPLIED;
    ok &= city_bestiary_choose_buddy_instance(&s_bestiary,2,personality_smoke_persist,NULL)==CITY_BESTIARY_APPLIED;
    ok &= city_bestiary_damage_instance(&s_bestiary,2,7,personality_smoke_persist,NULL)==CITY_BESTIARY_APPLIED;
    ok &= s_bestiary.owned[0].current_hp==35 && s_bestiary.owned[1].current_hp==28;
    ok &= city_bestiary_recover_instance(&s_bestiary,2,personality_smoke_persist,NULL)==CITY_BESTIARY_APPLIED;
    ok &= city_bestiary_visit(&s_bestiary,3,2,1,4,personality_smoke_persist,NULL)==CITY_BESTIARY_APPLIED;
    ok &= city_bestiary_visit(&s_bestiary,3,2,1,4,personality_smoke_persist,NULL)==CITY_BESTIARY_DUPLICATE;
    ok &= city_bestiary_visit(&s_bestiary,4,2,1,4,personality_smoke_persist,NULL)==CITY_BESTIARY_UNCHANGED;
    ok &= s_bestiary.owned[1].friendship==5 && s_bestiary.owned[0].friendship==0;
    s_companion_instance_id=s_visit_buddy_id=2;
    s_bestiary_selection=city_species_index(25);s_owned_selection=1;
    bsp_lvgl_unlock();
    ESP_LOGI(TAG,"PERSONALITY_SMOKE_DOMAIN pass=%u",ok);
    for(unsigned lang=0;lang<2;++lang) for(unsigned p=0;p<6;++p) {
        if(!bsp_lvgl_lock(2000)) { ok=false; goto finish; }
        s_settings.language=lang;
        s_bestiary.owned[1].personality=p;
        s_bestiary.owned[1].friendship=(p%3)*30;
        set_state(UI_OWNED_DETAIL);
        bsp_lvgl_unlock();vTaskDelay(pdMS_TO_TICKS(100));
        /* Exercise real button navigation; no write-producing action is selected. */
        on_button(BSP_BTN_OK,BSP_BTN_CLICK,NULL);
        ok &= s_state==UI_COMPANION && s_companion_instance_id==2;
        on_button(BSP_BTN_DOWN,BSP_BTN_CLICK,NULL);
        on_button(BSP_BTN_DOWN,BSP_BTN_CLICK,NULL);
        on_button(BSP_BTN_OK,BSP_BTN_CLICK,NULL);
        ok &= s_state==UI_OWNED_DETAIL;
        if(!bsp_lvgl_lock(2000)) { ok=false; goto finish; }
        s_visit_context=1;s_visit_gain=5;set_state(UI_BUDDY_REACTION);
        bsp_lvgl_unlock();vTaskDelay(pdMS_TO_TICKS(100));
        on_button(BSP_BTN_OK,BSP_BTN_CLICK,NULL);
        ok &= s_state==UI_ENCOUNTER;
        ESP_LOGI(TAG,"PERSONALITY_SMOKE_FRAME language=%u personality=%u pass=%u",lang,p,ok);
    }
finish:
    while(!bsp_lvgl_lock(2000)) vTaskDelay(1);
    s_bestiary=*original;s_settings.language=language;
    s_companion_recovered=false;s_visit_gain=0;s_owned_selection=0;
    set_state(UI_HOME);
    bsp_lvgl_unlock();free(original);
    ESP_LOGI(TAG,"PERSONALITY_SMOKE_%s heap_min=%lu stack_free=%u saved_data_untouched=1",
        ok?"PASS":"FAIL",(unsigned long)esp_get_minimum_free_heap_size(),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
    vTaskDelete(NULL);
}
