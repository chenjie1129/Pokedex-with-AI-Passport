#include "content_install.h"
#include <string.h>

typedef struct {
    uint32_t generation, slot, bytes, revision, high_revision;
    uint8_t manifest[32];
} record_t;
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t v = UINT32_MAX;
    while (n--) {
        v ^= *p++;
        for (unsigned i = 0; i < 8; ++i) v = (v >> 1) ^ (0xedb88320U & (0U - (v & 1U)));
    }
    return ~v;
}
static bool decode(const uint8_t raw[CITY_CONTENT_RECORD_BYTES], uint32_t capacity, record_t *r)
{
    if (memcmp(raw, "CITYACT1", 8) || u32(raw + 76) != crc32(raw, 76)) return false;
    for (unsigned i = 60; i < 76; ++i) if (raw[i]) return false;
    *r = (record_t){.generation = u32(raw + 8), .slot = u32(raw + 12),
        .bytes = u32(raw + 16), .revision = u32(raw + 20), .high_revision = u32(raw + 24)};
    memcpy(r->manifest, raw + 28, 32);
    return r->generation && r->slot < 2 && r->bytes >= 92 && r->bytes <= capacity &&
        r->revision && r->revision <= r->high_revision;
}
static bool read_slot(void *arg, uint32_t offset, void *out, size_t size)
{
    city_content_slot_t *slot = arg;
    city_content_io_t *io = &slot->store->io;
    return offset <= io->capacity && size <= io->capacity - offset &&
        io->read(io->context, slot->slot, offset, out, size);
}
static bool open_record(city_content_store_t *s, const record_t *r, city_pack_t *p)
{
    return city_pack_open(p, read_slot, &s->slots[r->slot], r->bytes, s->io.capacity, &s->crypto) &&
        p->revision == r->revision && !memcmp(p->manifest, r->manifest, 32) &&
        s->policy.accept(s->policy.context, p);
}
bool city_content_boot(city_content_store_t *s, const city_content_io_t *io, const city_pack_crypto_t *crypto,
                       const city_content_policy_t *policy)
{
    if (!s) return false;
    memset(s, 0, sizeof(*s)); s->active_slot = -1; s->active_bank = -1;
    if (!io || !crypto || !policy || !policy->accept) return false;
    if (!crypto->begin || !crypto->update || !crypto->finish || !crypto->verify) return false;
    if (!io->capacity || io->capacity > CITY_PACK_MAX_BYTES || !io->read || !io->erase ||
        !io->write || !io->read_record || !io->erase_record || !io->write_record) return false;
    s->io = *io; s->crypto = *crypto; s->policy = *policy;
    s->slots[0] = (city_content_slot_t){s, 0}; s->slots[1] = (city_content_slot_t){s, 1};
    uint32_t chosen = 0;
    for (unsigned bank = 0; bank < 2; ++bank) {
        uint8_t raw[CITY_CONTENT_RECORD_BYTES]; record_t r; city_pack_t pack;
        if (!io->read_record(io->context, bank, raw)) return false;
        if (!decode(raw, io->capacity, &r)) continue;
        if (r.generation > s->generation) s->generation = r.generation;
        if (r.high_revision > s->high_revision) s->high_revision = r.high_revision;
        if (r.generation > chosen && open_record(s, &r, &pack)) {
            s->active = pack; s->active_slot = (int)r.slot;
            s->active_bank = (int)bank; chosen = r.generation;
        }
    }
    if (s->active_slot < 0 && !s->policy.accept(s->policy.context, NULL)) return false;
    s->ready = true; return true;
}
static bool commit(city_content_store_t *s, unsigned slot, const city_pack_t *pack)
{
    if (s->generation == UINT32_MAX || !s->policy.accept(s->policy.context, pack)) return false;
    unsigned bank = s->active_bank == 0 ? 1U : 0U;
    uint8_t raw[CITY_CONTENT_RECORD_BYTES] = {0}, check[CITY_CONTENT_RECORD_BYTES];
    memcpy(raw, "CITYACT1", 8); put32(raw + 8, s->generation + 1);
    put32(raw + 12, slot); put32(raw + 16, pack->bytes); put32(raw + 20, pack->revision);
    uint32_t high = pack->revision > s->high_revision ? pack->revision : s->high_revision;
    put32(raw + 24, high); memcpy(raw + 28, pack->manifest, 32);
    put32(raw + 76, crc32(raw, 76));
    /* A driver error can occur after durable programming. Do not report the old
     * in-memory state as authoritative until boot has reconciled flash. */
    s->ready = false;
    if (!s->io.erase_record(s->io.context, bank) ||
        !s->io.write_record(s->io.context, bank, raw) ||
        !s->io.read_record(s->io.context, bank, check) || memcmp(raw, check, sizeof(raw))) return false;
    s->active = *pack; s->active_slot = (int)slot; s->active_bank = (int)bank;
    ++s->generation; s->high_revision = high; s->ready = true;
    return true;
}
bool city_content_install(city_content_store_t *s, city_pack_read_fn source, void *context, uint32_t bytes)
{
    if (!s || !s->ready || s->leases || !source || bytes > s->io.capacity ||
        s->generation == UINT32_MAX) return false;
    /* Validate source before erasing even the rollback slot. It must remain
     * immutable while read; staged bytes are independently verified afterward. */
    city_pack_t pack;
    if (!city_pack_open(&pack, source, context, bytes, s->io.capacity, &s->crypto) ||
        pack.revision <= s->high_revision || !s->policy.accept(s->policy.context, &pack)) return false;
    unsigned slot = s->active_slot == 0 ? 1U : 0U;
    if (!s->io.erase(s->io.context, slot)) return false;
    uint8_t block[CITY_PACK_READ_BYTES];
    for (uint32_t offset = 0; offset < bytes;) {
        size_t size = bytes - offset < sizeof(block) ? bytes - offset : sizeof(block);
        if (!source(context, offset, block, size) ||
            !s->io.write(s->io.context, slot, offset, block, size)) return false;
        offset += (uint32_t)size;
    }
    uint8_t expected[32]; memcpy(expected, pack.manifest, sizeof(expected));
    if (!city_pack_open(&pack, read_slot, &s->slots[slot], bytes, s->io.capacity, &s->crypto) ||
        memcmp(expected, pack.manifest, sizeof(expected))) return false;
    return commit(s, slot, &pack);
}
bool city_content_rollback(city_content_store_t *s)
{
    if (!s || !s->ready || s->leases || s->active_bank < 0 || s->generation == UINT32_MAX) return false;
    unsigned bank = s->active_bank == 0 ? 1U : 0U;
    uint8_t raw[CITY_CONTENT_RECORD_BYTES]; record_t r; city_pack_t pack;
    if (!s->io.read_record(s->io.context, bank, raw) || !decode(raw, s->io.capacity, &r) ||
        r.slot == (unsigned)s->active_slot || !open_record(s, &r, &pack)) return false;
    return commit(s, r.slot, &pack);
}
const city_pack_t *city_content_acquire(city_content_store_t *s)
{
    if (!s || !s->ready || s->active_slot < 0 || s->leases == UINT32_MAX) return NULL;
    ++s->leases; return &s->active;
}
bool city_content_release(city_content_store_t *s, const city_pack_t *pack)
{
    if (!s || pack != &s->active || !s->leases) return false;
    --s->leases; return true;
}

bool city_content_activate_staged(city_content_store_t *s, unsigned slot, uint32_t bytes)
{
    if (!s || !s->ready || s->leases || slot > 1 || slot == (unsigned)s->active_slot ||
        bytes > s->io.capacity || s->generation == UINT32_MAX) return false;
    city_pack_t pack;
    if (!city_pack_open(&pack, read_slot, &s->slots[slot], bytes, s->io.capacity, &s->crypto) ||
        pack.revision <= s->high_revision || !s->policy.accept(s->policy.context, &pack)) return false;
    return commit(s, slot, &pack);
}
