#include "content_pack.h"
#include <string.h>

#define HEADER_BYTES 28U
#define ENTRY_BYTES 44U
#define SIGNATURE_BYTES 64U
#define META_BYTES 376U
#define OBJECT_LIMIT (1024U * 1024U)

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static bool rd(const city_pack_t *p, uint32_t offset, void *out, size_t length)
{
    return p && p->read && out && length <= CITY_PACK_READ_BYTES &&
        offset <= p->bytes && length <= p->bytes - offset &&
        p->read(p->context, offset, out, length);
}

static bool entry(const city_pack_t *p, uint32_t i, uint8_t raw[ENTRY_BYTES])
{ return i < p->count * 3U && rd(p, HEADER_BYTES + i * ENTRY_BYTES, raw, ENTRY_BYTES); }

static bool utf8(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n;) {
        uint32_t code = p[i++], minimum = 0;
        unsigned more = 0;
        if (code < 0x80) { if (code < 0x20 || code == 0x7f) return false; continue; }
        if (code >= 0xc2 && code <= 0xdf) { more = 1; code &= 0x1f; minimum = 0x80; }
        else if (code >= 0xe0 && code <= 0xef) { more = 2; code &= 0xf; minimum = 0x800; }
        else if (code >= 0xf0 && code <= 0xf4) { more = 3; code &= 7; minimum = 0x10000; }
        else return false;
        if (more > n - i) return false;
        while (more--) {
            uint8_t next = p[i++];
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
            (code >= 0x80 && code <= 0x9f)) return false;
    }
    return true;
}

static bool metadata(const city_pack_t *p, uint32_t index, city_pack_species_t *out)
{
    uint8_t e[ENTRY_BYTES], b[META_BYTES];
    if (!entry(p, index * 3U, e)) return false;
    uint32_t length = u32(e + 8), offset = u32(e + 4);
    if (length < 26 || length > sizeof(b) || offset > p->bytes - p->payload ||
        length > p->bytes - p->payload - offset || !rd(p, p->payload + offset, b, length)) return false;
    city_pack_species_t next = {0};
    next.species_id = u16(b + 6); next.evolves_from = u16(b + 8);
    next.flags = b[10]; next.pool = b[11]; next.type1 = b[12]; next.type2 = b[13];
    next.hp = b[14]; next.attack = b[15]; next.defense = b[16];
    if (memcmp(b, "CM01", 4) || u16(b + 4) != 1 || next.species_id != u16(e) ||
        (next.flags & ~7U) || next.pool > 2 || !next.type1 || next.type1 > 18 || next.type2 > 18 ||
        next.type1 == next.type2 || !next.hp || next.hp > 240 || !next.attack || next.attack > 240 ||
        !next.defense || next.defense > 240 || b[17] || next.evolves_from == next.species_id ||
        next.evolves_from == UINT16_MAX || (next.evolves_from && (next.flags & 3U)) ||
        ((next.species_id >= 60000) != ((next.flags & 4U) != 0))) return false;
    char *strings[] = {next.name_en, next.name_zh, next.description_en, next.description_zh};
    const size_t capacities[] = {sizeof(next.name_en), sizeof(next.name_zh),
        sizeof(next.description_en), sizeof(next.description_zh)};
    size_t cursor = 26;
    for (unsigned i = 0; i < 4; ++i) {
        size_t size = u16(b + 18 + i * 2);
        if (!size || size >= capacities[i] || size > length - cursor || !utf8(b + cursor, size)) return false;
        memcpy(strings[i], b + cursor, size); cursor += size;
    }
    if (cursor != length) return false;
    *out = next; return true;
}

static bool find_index(const city_pack_t *p, uint16_t id, uint32_t *index)
{
    uint32_t low = 0, high = p->count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2U;
        uint8_t e[ENTRY_BYTES];
        if (!entry(p, middle * 3U, e)) return false;
        uint16_t sid = u16(e);
        if (sid == id) { *index = middle; return true; }
        if (sid < id) low = middle + 1U; else high = middle;
    }
    return false;
}

static bool typed_asset(const city_pack_t *p, const uint8_t e[ENTRY_BYTES])
{
    uint8_t b[12];
    uint32_t length = u32(e + 8);
    if (length < sizeof(b) || !rd(p, p->payload + u32(e + 4), b, sizeof(b))) return false;
    if (e[2] == 2) {
        uint32_t w = u16(b + 4), h = u16(b + 6);
        return !memcmp(b, "CS01", 4) && w && h && w <= 110 && h <= 110 &&
            b[8] == 1 && !b[9] && !b[10] && !b[11] && length == 12U + w * h * 3U;
    }
    uint32_t samples = u32(b + 8);
    return e[2] == 3 && !memcmp(b, "CA01", 4) && u16(b + 4) == 16000 &&
        b[6] == 1 && b[7] == 1 && samples && samples <= 32000 && length == 12U + samples * 2U;
}

static bool hash_object(const city_pack_t *p, const uint8_t e[ENTRY_BYTES], const city_pack_crypto_t *c)
{
    uint8_t block[CITY_PACK_READ_BYTES], digest[32];
    uint32_t start = p->payload + u32(e + 4), remain = u32(e + 8);
    if (!c->begin(c->context)) return false;
    while (remain) {
        size_t size = remain < sizeof(block) ? remain : sizeof(block);
        if (!rd(p, start, block, size) || !c->update(c->context, block, size)) return false;
        remain -= (uint32_t)size; start += (uint32_t)size;
    }
    return c->finish(c->context, digest) && !memcmp(digest, e + 12, 32);
}

bool city_pack_open(city_pack_t *out, city_pack_read_fn read, void *context,
                    uint32_t bytes, uint32_t budget, const city_pack_crypto_t *c)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!read || !c || !c->begin || !c->update || !c->finish || !c->verify ||
        !budget || budget > CITY_PACK_MAX_BYTES || bytes > budget || bytes < 92) return false;
    city_pack_t p = {.read = read, .context = context, .bytes = bytes};
    uint8_t h[HEADER_BYTES], e[ENTRY_BYTES], digest[32], signature[SIGNATURE_BYTES];
    if (!rd(&p, 0, h, sizeof(h)) || memcmp(h, "CITYPK01", 8) || u16(h + 8) != 1 ||
        (u16(h + 10) != CITY_PACK_ED25519 && u16(h + 10) != CITY_PACK_P256) || !u32(h + 12) || u32(h + 24)) return false;
    uint32_t objects = u32(h + 16), payload = u32(h + 20);
    if (!objects || objects > CITY_PACK_MAX_SPECIES * 3U || objects % 3U) return false;
    p.count = objects / 3U; p.revision = u32(h + 12);
    p.payload = HEADER_BYTES + objects * ENTRY_BYTES + SIGNATURE_BYTES;
    if (p.payload > bytes || payload != bytes - p.payload || !c->begin(c->context) ||
        !c->update(c->context, h, sizeof(h))) return false;
    uint32_t offset = 0; uint16_t species = 0;
    for (uint32_t i = 0; i < objects; ++i) {
        if (!entry(&p, i, e) || !c->update(c->context, e, sizeof(e))) return false;
        uint16_t sid = u16(e); uint32_t size = u32(e + 8);
        if (!sid || sid == UINT16_MAX || e[2] != i % 3U + 1U || e[3] ||
            (i % 3U == 0 ? sid <= species : sid != species) ||
            !size || size > OBJECT_LIMIT || u32(e + 4) != offset || size > payload - offset) return false;
        species = sid; offset += size;
    }
    if (offset != payload || !c->finish(c->context, digest) ||
        !rd(&p, p.payload - SIGNATURE_BYTES, signature, sizeof(signature)) ||
        !c->verify(c->context, u16(h + 10), digest, signature)) return false;
    for (uint32_t i = 0; i < objects; ++i) {
        if (!entry(&p, i, e) || u32(e + 4) > payload || u32(e + 8) > payload - u32(e + 4) ||
            !hash_object(&p, e, c)) return false;
        if (i % 3U == 0) {
            city_pack_species_t metadata_value;
            if (!metadata(&p, i / 3U, &metadata_value)) return false;
            if (metadata_value.evolves_from) {
                uint32_t parent;
                if (!find_index(&p, metadata_value.evolves_from, &parent) ||
                    !metadata(&p, parent, &metadata_value) || metadata_value.evolves_from) return false;
            }
        } else if (!typed_asset(&p, e)) return false;
    }
    memcpy(p.manifest, digest, sizeof(p.manifest));
    p.verified = true; *out = p; return true;
}

bool city_pack_at(const city_pack_t *p, uint32_t index, city_pack_species_t *out)
{ return p && p->verified && out && index < p->count && metadata(p, index, out); }

bool city_pack_find(const city_pack_t *p, uint16_t id, city_pack_species_t *out)
{
    uint32_t index;
    return p && p->verified && out && find_index(p, id, &index) && metadata(p, index, out);
}

bool city_pack_asset(const city_pack_t *p, uint16_t id, uint8_t kind, city_pack_asset_t *out)
{
    uint32_t index; uint8_t e[ENTRY_BYTES];
    if (!p || !p->verified || !out || (kind != 2 && kind != 3) || !find_index(p, id, &index) ||
        !entry(p, index * 3U + kind - 1U, e)) return false;
    uint32_t offset = u32(e + 4), length = u32(e + 8);
    if (offset > p->bytes - p->payload || length > p->bytes - p->payload - offset) return false;
    *out = (city_pack_asset_t){p->payload + offset, length}; return true;
}

bool city_pack_read_asset(const city_pack_t *p, uint16_t id, uint8_t kind,
                          uint32_t offset, void *out, size_t length)
{
    city_pack_asset_t asset;
    return length && length <= CITY_PACK_READ_BYTES && city_pack_asset(p, id, kind, &asset) &&
        offset <= asset.length && length <= asset.length - offset && rd(p, asset.offset + offset, out, length);
}
