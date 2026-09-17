#include "bsp_content_storage.h"
#include <string.h>

typedef struct { const char *label; unsigned type, subtype; uint32_t offset, size; } layout_t;
static const layout_t expected[] = {
    {"nvs", 1, 2, 0x9000, 0x6000}, {"phy_init", 1, 1, 0xf000, 0x1000},
    {"factory", 0, 0, 0x10000, 0x300000}, {"cardid", 1, 2, 0x356000, 0x4000},
    {"recovery", 0, 0x20, 0x700000, 0x100000},
    {"content_a", 1, 0x40, 0x360000, BSP_CONTENT_SLOT_BYTES},
    {"content_b", 1, 0x40, 0x500000, BSP_CONTENT_SLOT_BYTES},
    {"content_ctl", 1, 0x41, 0x6a0000, BSP_CONTENT_SECTOR_BYTES * 2}
};
static bool bounds(bsp_content_storage_t *s, unsigned slot, uint32_t offset, size_t size)
{
    return s && s->ready && slot < 2 && size && size <= CITY_PACK_READ_BYTES &&
        offset <= BSP_CONTENT_SLOT_BYTES && size <= BSP_CONTENT_SLOT_BYTES - offset;
}
static bool read_slot(void *arg, unsigned slot, uint32_t offset, void *out, size_t size)
{
    bsp_content_storage_t *s = arg;
    return out && bounds(s, slot, offset, size) &&
        esp_partition_read(s->slots[slot], offset, out, size) == ESP_OK;
}
static bool write_slot(void *arg, unsigned slot, uint32_t offset, const void *data, size_t size)
{
    bsp_content_storage_t *s = arg;
    return data && bounds(s, slot, offset, size) &&
        esp_partition_write(s->slots[slot], offset, data, size) == ESP_OK;
}
static bool erase_slot(void *arg, unsigned slot)
{
    bsp_content_storage_t *s = arg;
    return s && s->ready && slot < 2 &&
        esp_partition_erase_range(s->slots[slot], 0, BSP_CONTENT_SLOT_BYTES) == ESP_OK;
}
static bool read_record(void *arg, unsigned bank, uint8_t out[CITY_CONTENT_RECORD_BYTES])
{
    bsp_content_storage_t *s = arg;
    return s && s->ready && bank < 2 && out &&
        esp_partition_read(s->control, bank * BSP_CONTENT_SECTOR_BYTES, out, CITY_CONTENT_RECORD_BYTES) == ESP_OK;
}
static bool erase_record(void *arg, unsigned bank)
{
    bsp_content_storage_t *s = arg;
    return s && s->ready && bank < 2 && esp_partition_erase_range(s->control,
        bank * BSP_CONTENT_SECTOR_BYTES, BSP_CONTENT_SECTOR_BYTES) == ESP_OK;
}
static bool write_record(void *arg, unsigned bank, const uint8_t data[CITY_CONTENT_RECORD_BYTES])
{
    bsp_content_storage_t *s = arg;
    return s && s->ready && bank < 2 && data &&
        esp_partition_write(s->control, bank * BSP_CONTENT_SECTOR_BYTES, data, CITY_CONTENT_RECORD_BYTES) == ESP_OK;
}
bool bsp_content_storage_open(bsp_content_storage_t *s, city_content_io_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!s) return false;
    memset(s, 0, sizeof(*s));
    if (!out) return false;
    const esp_partition_t *found[8] = {0};
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t *p = esp_partition_get(it);
        unsigned index = 0;
        for (; index < 8; ++index) if (!strcmp(p->label, expected[index].label)) break;
        if (index == 8 || found[index] || p->encrypted || p->readonly ||
            p->type != expected[index].type || p->subtype != expected[index].subtype ||
            p->address != expected[index].offset || p->size != expected[index].size) {
            esp_partition_iterator_release(it); return false;
        }
        found[index] = p; it = esp_partition_next(it);
    }
    for (unsigned i = 0; i < 8; ++i)
        if (!found[i] || found[i]->flash_chip != found[0]->flash_chip) return false;
    s->slots[0] = found[5]; s->slots[1] = found[6]; s->control = found[7]; s->ready = true;
    *out = (city_content_io_t){s, BSP_CONTENT_SLOT_BYTES, read_slot, erase_slot, write_slot,
        read_record, erase_record, write_record};
    return true;
}
