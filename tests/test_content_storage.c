#include "bsp_content_storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static esp_partition_t parts[9];
static const esp_partition_t expected[] = {
    {.label="nvs", .type=1, .subtype=2, .address=0x9000, .size=0x6000},
    {.label="phy_init", .type=1, .subtype=1, .address=0xf000, .size=0x1000},
    {.label="factory", .type=0, .subtype=0, .address=0x10000, .size=0x300000},
    {.label="cardid", .type=1, .subtype=2, .address=0x356000, .size=0x4000},
    {.label="recovery", .type=0, .subtype=0x20, .address=0x700000, .size=0x100000},
    {.label="content_a", .type=1, .subtype=0x40, .address=0x420000, .size=0x140000},
    {.label="content_b", .type=1, .subtype=0x40, .address=0x560000, .size=0x140000},
    {.label="content_ctl", .type=1, .subtype=0x41, .address=0x6a0000, .size=0x2000}
};
struct test_iterator { unsigned index; };
static struct test_iterator iterator;
static unsigned count, calls, live;
static int result;
static const esp_partition_t *last;
static size_t last_offset, last_size;
esp_partition_iterator_t esp_partition_find(unsigned type, unsigned subtype, const char *label)
{ assert(type == 0xff && subtype == 0xff && !label && !live); iterator.index = 0; live = count != 0; return live ? &iterator : NULL; }
const esp_partition_t *esp_partition_get(esp_partition_iterator_t it)
{ assert(live && it->index < count); return &parts[it->index]; }
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t it)
{ if (++it->index < count) return it; live = 0; return NULL; }
void esp_partition_iterator_release(esp_partition_iterator_t it)
{ assert(it && live); live = 0; }
static int accessed(const esp_partition_t *p, size_t offset, size_t size)
{ assert(p >= &parts[5] && p <= &parts[7]); assert(offset <= p->size && size <= p->size - offset);
  ++calls; last = p; last_offset = offset; last_size = size; return result; }
int esp_partition_read(const esp_partition_t *p, size_t offset, void *out, size_t size)
{ assert(out); memset(out, 0xff, size); return accessed(p, offset, size); }
int esp_partition_write(const esp_partition_t *p, size_t offset, const void *data, size_t size)
{ assert(data); return accessed(p, offset, size); }
int esp_partition_erase_range(const esp_partition_t *p, size_t offset, size_t size)
{ assert(offset % 4096 == 0 && size % 4096 == 0); return accessed(p, offset, size); }
static void reset(void)
{ memcpy(parts, expected, sizeof(expected)); count = 8; calls = 0; result = 0; assert(!live); }
static void reject(void)
{
    bsp_content_storage_t s; city_content_io_t io;
    assert(!bsp_content_storage_open(&s, &io)); assert(!s.ready && !io.read && !calls && !live);
}
int main(void)
{
    reset(); count = 5; reject(); /* Exact current shipping partition table. */
    for (unsigned i = 0; i < 8; ++i) {
        reset(); parts[i].address += 4096; reject();
        reset(); parts[i].size += 4096; reject();
        reset(); parts[i].type ^= 1; reject();
        reset(); parts[i].subtype ^= 1; reject();
        reset(); parts[i].encrypted = true; reject();
        reset(); parts[i].readonly = true; reject();
        reset(); parts[i].label[0] = 'X'; reject();
        reset(); parts[i].flash_chip = &parts; reject();
    }
    reset(); parts[8] = parts[5]; count = 9; reject();
    reset(); strcpy(parts[8].label, "extra"); count = 9; reject();
    reset(); parts[5].address = parts[3].address; reject(); /* Identity overlap. */
    reset(); bsp_content_storage_t s; city_content_io_t io;
    assert(bsp_content_storage_open(&s, &io) && !calls && !live);
    uint8_t data[512] = {0};
    assert(io.read(io.context, 0, 0, data, sizeof(data)));
    assert(io.write(io.context, 1, BSP_CONTENT_SLOT_BYTES - 512, data, 512));
    assert(io.erase(io.context, 0)); assert(last == &parts[5] && last_size == BSP_CONTENT_SLOT_BYTES);
    assert(io.erase_record(io.context, 1)); assert(last == &parts[7] && last_offset == 4096 && last_size == 4096);
    assert(io.write_record(io.context, 1, data)); assert(last_offset == 4096 && last_size == 80);
    assert(io.read_record(io.context, 0, data));
    unsigned before = calls;
    assert(!io.read(io.context, 2, 0, data, 1));
    assert(!io.read(io.context, 0, UINT32_MAX, data, 1));
    assert(!io.read(io.context, 0, 0, data, 513));
    assert(!io.write(io.context, 1, BSP_CONTENT_SLOT_BYTES, data, 1));
    assert(!io.write(io.context, 1, 0, NULL, 1));
    assert(!io.erase(io.context, 2)); assert(!io.erase_record(io.context, 2));
    assert(!io.read_record(io.context, 2, data)); assert(!io.write_record(io.context, 2, data));
    assert(calls == before);
    result = -1;
    assert(!io.read(io.context, 0, 0, data, 1)); assert(!io.write(io.context, 0, 0, data, 1));
    assert(!io.erase(io.context, 0)); assert(!io.read_record(io.context, 0, data));
    assert(!io.write_record(io.context, 0, data)); assert(!io.erase_record(io.context, 0));
    puts("Protected layout, bounds, sector isolation and I/O failures passed"); return 0;
}
