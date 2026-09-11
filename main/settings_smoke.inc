/* Included only inside app_main in the opt-in device diagnostic.
 * Timers/input stay disabled: only the settings worker can write storage.
 */
    const city_settings_t original = s_settings;
    bool smoke_ok = true;
    if (!bsp_lvgl_lock(2000)) return;
    s_settings_draft = original;
    s_settings_selection = 0;
    s_settings_editing = false;
    set_state(UI_SETTINGS);
    handle_settings_button(BSP_BTN_OK, false);
    handle_settings_button(BSP_BTN_DOWN, false);
    handle_settings_button(BSP_BTN_OK, false);
    smoke_ok &= s_settings_draft.volume == (original.volume >= 10 ? original.volume - 10 : 0);
    handle_settings_button(BSP_BTN_OK, true);
    smoke_ok &= s_state == UI_HOME && city_settings_equal(&s_settings, &original);
    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "SETTINGS_SMOKE_CANCEL pass=%u", smoke_ok);

    /* Real queue, worker, NVS commit and independent load; restore even on failure. */
    for (unsigned phase = 0; phase < 2; ++phase) {
        const city_settings_t expected = phase == 0
            ? (city_settings_t){.volume = 30, .brightness = 80, .muted = true} : original;
        if (!bsp_lvgl_lock(2000)) return;
        s_settings_draft = expected;
        s_settings_selection = 1;
        s_settings_editing = false;
        set_state(UI_SETTINGS);
        handle_settings_button(BSP_BTN_OK, false);
        smoke_ok &= s_settings_draft.muted != expected.muted;
        handle_settings_button(BSP_BTN_OK, false);
        s_settings_selection = 2;
        handle_settings_button(BSP_BTN_OK, false);
        handle_settings_button(BSP_BTN_DOWN, false);
        handle_settings_button(BSP_BTN_UP, false);
        handle_settings_button(BSP_BTN_OK, false);
        s_settings_draft = expected;
        apply_settings_preview();
        s_settings_selection = 3;
        handle_settings_button(BSP_BTN_OK, false);
        bool pending = s_settings_saving;
        bsp_lvgl_unlock();
        settings_result_t result;
        if (pending) {
            if (xQueueReceive(s_settings_results, &result, pdMS_TO_TICKS(5000)) != pdTRUE) {
                ESP_LOGE(TAG, "SETTINGS_SMOKE_FAIL timeout"); return;
            }
            if (!bsp_lvgl_lock(2000)) return;
            finish_settings_save(&result);
            smoke_ok &= result.error == ESP_OK && s_state == UI_HOME;
            bsp_lvgl_unlock();
        }
        city_settings_t loaded;
        smoke_ok &= bsp_settings_load(&loaded) == ESP_OK && city_settings_equal(&loaded, &expected);
        ESP_LOGI(TAG, "SETTINGS_SMOKE_STORAGE phase=%u pass=%u", phase, smoke_ok);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG, "SETTINGS_SMOKE_%s captures=%lu", smoke_ok ? "PASS" : "FAIL", (unsigned long)total_capture_count());
    return;
