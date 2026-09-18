#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "species_catalog.h"
#include "bestiary_service.h"
#include "../main/pokemon_audio.c"

static jmp_buf idle;
static bool queue_full, queue_failure, task_failure, codec_failure, write_failure;
static uint16_t queued, inject;
static unsigned writes, volume, audible_starts, samples_written, initializations;
static const pokemon_cry_t *expected;
static bool package_enabled, package_read_failure;
static unsigned package_reads;

QueueHandle_t xQueueCreate(unsigned count, unsigned size)
{
    assert(count == 1 && size == sizeof(uint16_t));
    return queue_failure ? NULL : &queued;
}
void vQueueDelete(QueueHandle_t queue) { assert(queue == &queued); }
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *value)
{
    assert(queue == &queued);
    memcpy(&queued, value, sizeof(queued)); queue_full = true; return pdTRUE;
}
BaseType_t xQueuePeek(QueueHandle_t queue, void *value, uint32_t timeout)
{
    assert(queue == &queued && timeout == 0);
    if (!queue_full) return 0;
    memcpy(value, &queued, sizeof(queued)); return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *value, uint32_t timeout)
{
    assert(timeout == portMAX_DELAY);
    if (!xQueuePeek(queue, value, 0)) longjmp(idle, 1);
    queue_full = false;
    return pdTRUE;
}
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *argument, unsigned priority, void *handle)
{
    assert(task == audio_task && stack == 4096);
    (void)name; (void)argument; (void)priority; (void)handle;
    return task_failure ? 0 : pdPASS;
}
esp_err_t bsp_audio_init(void) { ++initializations; return codec_failure ? ESP_FAIL : ESP_OK; }
esp_err_t bsp_audio_set_format(uint32_t rate, uint8_t bits, uint8_t channels)
{
    assert(rate == 16000 && bits == 16 && channels == 1); return ESP_OK;
}
void bsp_audio_set_volume(uint8_t percent)
{
    if (percent) { assert(percent <= 100); if (!volume) { ++audible_starts; samples_written = 0; } }
    volume = percent;
}
esp_err_t bsp_audio_write(const void *pcm, size_t bytes)
{
    assert(bytes > 0 && bytes <= 640 && bytes % 2 == 0);
    ++writes;
    if (volume && samples_written < expected->sample_count) {
        assert(samples_written + bytes / 2 <= expected->sample_count);
        assert(memcmp(pcm, expected->samples + samples_written, bytes) == 0);
        samples_written += bytes / 2;
    } else {
        const int16_t *data = pcm;
        for (size_t i = 0; i < bytes / 2; ++i) assert(data[i] == 0);
    }
    if (inject) {
        const uint16_t next = inject;
        inject = 0;
        if (next == UINT16_MAX - 1) pokemon_audio_set_preferences(60, true);
        else if (next == UINT16_MAX) pokemon_audio_stop();
        else { expected = pokemon_cry_find(next); pokemon_audio_play(next); }
    }
    return write_failure ? ESP_FAIL : ESP_OK;
}
static void run_worker(void)
{
    if (!setjmp(idle)) audio_task(NULL);
    assert(volume == 0);
}
static void reset(void)
{
    package_enabled=false; package_read_failure=false; package_reads=0;
    requests = NULL; atomic_store(&preferences, 60U); queue_full = false; queue_failure = false; task_failure = false;
    codec_failure = false; write_failure = false; queued = 0; inject = 0;
    writes = 0; audible_starts = 0; samples_written = 0; initializations = 0;
}
int main(void)
{
    reset(); pokemon_audio_play(1); pokemon_audio_stop(); assert(!queue_full);
    queue_failure = true; assert(!pokemon_audio_init());
    queue_failure = false; task_failure = true; assert(!pokemon_audio_init() && !requests);
    task_failure = false; assert(pokemon_audio_init()); assert(pokemon_audio_init());
    for (unsigned i = 0; i < CITY_SPECIES_COUNT; ++i) {
        reset(); assert(pokemon_audio_init());
        const uint16_t id = city_species_id_at(i);
        expected = pokemon_cry_find(id);
        assert(expected && expected->species_id == id);
        assert(expected->sample_count >= 1600 && expected->sample_count <= 64000);
        pokemon_audio_play(id); assert(writes == 0); run_worker();
        assert(audible_starts == 1 && samples_written == expected->sample_count);
    }
    assert(!pokemon_cry_find(0) && !pokemon_cry_find(999));
    reset(); assert(pokemon_audio_init()); pokemon_audio_play(999); run_worker(); assert(!writes);
    reset(); assert(pokemon_audio_init()); expected = pokemon_cry_find(25);
    pokemon_audio_play(1); pokemon_audio_play(4); pokemon_audio_play(25);
    run_worker(); assert(audible_starts == 1 && samples_written == expected->sample_count);
    reset(); assert(pokemon_audio_init()); pokemon_audio_play(1); pokemon_audio_stop();
    run_worker(); assert(!writes);
    reset(); assert(pokemon_audio_init()); expected = pokemon_cry_find(1); inject = UINT16_MAX;
    pokemon_audio_play(1); run_worker(); assert(audible_starts == 1 && samples_written == 320);
    reset(); assert(pokemon_audio_init()); expected = pokemon_cry_find(1); inject = 25;
    pokemon_audio_play(1); run_worker(); assert(audible_starts == 2 && samples_written == expected->sample_count);
    reset(); assert(pokemon_audio_init()); codec_failure = true;
    pokemon_audio_play(1); run_worker(); assert(!writes && initializations == 1);
    reset(); assert(pokemon_audio_init()); expected = pokemon_cry_find(1); write_failure = true;
    pokemon_audio_play(1); run_worker(); assert(writes == 2 && volume == 0);
    reset(); assert(pokemon_audio_init()); pokemon_audio_set_preferences(60, true);
    pokemon_audio_play(1); run_worker(); assert(!writes && !audible_starts);
    pokemon_audio_set_preferences(60, false); expected = pokemon_cry_find(1);
    pokemon_audio_play(1); run_worker(); assert(audible_starts == 1);
    reset(); assert(pokemon_audio_init()); pokemon_audio_set_preferences(0, false);
    pokemon_audio_play(1); run_worker(); assert(!writes);
    reset(); assert(pokemon_audio_init()); pokemon_audio_set_preferences(30, false);
    assert(effective_volume() == 30); expected = pokemon_cry_find(1);
    pokemon_audio_play(1); run_worker(); assert(audible_starts == 1);
    pokemon_audio_set_preferences(255, false); assert(effective_volume() == 100);
    pokemon_audio_set_preferences(30, true); assert(effective_volume() == 0);
    pokemon_audio_set_preferences(30, false); assert(effective_volume() == 30);
    reset(); assert(pokemon_audio_init()); expected = pokemon_cry_find(1); inject = UINT16_MAX - 1;
    pokemon_audio_play(1); run_worker(); assert(audible_starts == 1 && samples_written == 320);
    reset(); assert(pokemon_audio_init()); expected=pokemon_cry_find(25); package_enabled=true;
    pokemon_audio_play(60001); run_worker();
    assert(package_reads>1 && samples_written==expected->sample_count);
    reset(); assert(pokemon_audio_init()); expected=pokemon_cry_find(25); package_enabled=true; package_read_failure=true;
    pokemon_audio_play(60001); run_worker(); assert(package_reads==1 && samples_written==0 && volume==0);
    reset(); assert(pokemon_audio_init()); expected=pokemon_cry_find(25); package_enabled=true; inject=UINT16_MAX;
    pokemon_audio_play(60001); run_worker(); assert(samples_written==256 && package_reads==1);
    puts("Audio mapping, PCM integrity, volume, mute, cancellation and failures passed");
    return 0;
}

bool pokemon_content_cry_size(uint16_t id, uint32_t *n)
{
    if (!package_enabled || id!=60001) return false;
    *n=(uint32_t)expected->sample_count; return true;
}
bool pokemon_content_cry_read(uint16_t id, uint32_t sample, void *out, size_t n)
{
    assert(package_enabled && id==60001 && n<=512 && n%2==0);
    assert(sample+n/2<=expected->sample_count); ++package_reads;
    if (package_read_failure) return false;
    memcpy(out,expected->samples+sample,n); return true;
}
