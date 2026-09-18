#include "pokemon_audio.h"
#include "pokemon_cries.h"
#include "pokemon_content_audio.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "pokemon_audio";
static QueueHandle_t requests;
static atomic_uint preferences = ATOMIC_VAR_INIT(60U);

static uint8_t effective_volume(void)
{
    const unsigned value = atomic_load(&preferences);
    return (value & 0x100U) ? 0 : (uint8_t)(value & 0xffU);
}

static void audio_task(void *argument)
{
    (void)argument;
    /* Only this worker owns the codec; never call blocking I2S from LVGL. */
    const bool ready = bsp_audio_init() == ESP_OK &&
        bsp_audio_set_format(POKEMON_CRY_SAMPLE_RATE, 16, 1) == ESP_OK;
    bsp_audio_set_volume(0);
    ESP_LOGI(TAG, "AUDIO_READY ready=%u", ready);
    int16_t buffer[320]; /* 20 ms, copied from flash into DMA-safe RAM. */
    for (;;) {
        uint16_t species_id;
        if (xQueueReceive(requests, &species_id, portMAX_DELAY) != pdTRUE) continue;
        const pokemon_cry_t *cry = pokemon_cry_find(species_id);
        uint32_t pack_samples = 0;
        bool packaged = pokemon_content_cry_size(species_id, &pack_samples);
        size_t sample_count = packaged ? pack_samples : cry ? cry->sample_count : 0;
        if (!ready || !sample_count || effective_volume() == 0) continue;
        uint16_t pending;
        if (xQueuePeek(requests, &pending, 0) == pdTRUE) continue;
        uint8_t playing_volume = effective_volume();
        bsp_audio_set_volume(playing_volume);
        ESP_LOGI(TAG, "CRY_START species=%u samples=%u", species_id, (unsigned)sample_count);
        size_t offset = 0;
        bool failed = false;
        while (offset < sample_count) {
            if (xQueuePeek(requests, &pending, 0) == pdTRUE) break;
            const uint8_t next_volume = effective_volume();
            if (next_volume == 0) break;
            if (next_volume != playing_volume) {
                bsp_audio_set_volume(next_volume);
                playing_volume = next_volume;
            }
            size_t count = sample_count - offset;
            const size_t block = packaged ? 256 : 320;
            if (count > block) count = block;
            if (packaged) {
                if (!pokemon_content_cry_read(species_id, offset, buffer, count * sizeof(int16_t))) { failed = true; break; }
            } else memcpy(buffer, cry->samples + offset, count * sizeof(int16_t));
            if (bsp_audio_write(buffer, count * sizeof(int16_t)) != ESP_OK) {
                failed = true;
                break;
            }
            offset += count;
        }
        /* Flush the six 240-frame DMA buffers with silence so the tail finishes
           and stale sound cannot reappear when unmuting the next request. */
        if (offset < sample_count || failed) bsp_audio_set_volume(0);
        memset(buffer, 0, sizeof(buffer));
        for (unsigned i = 0; i < 6; ++i)
            if (bsp_audio_write(buffer, sizeof(buffer)) != ESP_OK) { failed = true; break; }
        bsp_audio_set_volume(0);
        ESP_LOGI(TAG, "CRY_END species=%u complete=%u failed=%u", species_id,
                 offset == sample_count && !failed, failed);
    }
}

bool pokemon_audio_init(void)
{
    if (requests) return true;
    requests = xQueueCreate(1, sizeof(uint16_t));
    if (!requests) return false;
    if (xTaskCreate(audio_task, "pokemon_audio", 4096, NULL, 2, NULL) != pdPASS) {
        vQueueDelete(requests);
        requests = NULL;
        return false;
    }
    return true;
}

void pokemon_audio_play(uint16_t species_id)
{
    if (requests) xQueueOverwrite(requests, &species_id);
}

void pokemon_audio_stop(void)
{
    pokemon_audio_play(0);
}

void pokemon_audio_set_preferences(uint8_t volume, bool muted)
{
    if (volume > 100) volume = 100;
    atomic_store(&preferences, volume | (muted ? 0x100U : 0));
    if (muted || volume == 0) pokemon_audio_stop();
}
