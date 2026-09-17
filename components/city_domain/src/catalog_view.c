#include "catalog_view.h"
#include <string.h>

static bool live(const city_catalog_view_t *v)
{
    return v && v->open && (!v->store || (v->store->ready && v->store->leases &&
        v->pack == &v->store->active && v->pack->verified));
}
bool city_catalog_open_compiled(city_catalog_view_t *v)
{
    if (!v || v->open) return false;
    memset(v, 0, sizeof(*v));
    v->count = CITY_CATALOG_DEFAULT.count; v->revision = CITY_CATALOG_DEFAULT.revision;
    v->open = true; return true;
}
bool city_catalog_open_installed(city_catalog_view_t *v, city_content_store_t *s)
{
    if (!v || v->open) return false;
    const city_pack_t *pack = city_content_acquire(s);
    if (!pack) return false;
    memset(v, 0, sizeof(*v)); v->store = s; v->pack = pack;
    v->count = pack->count; v->revision = pack->revision; v->open = true; return true;
}
bool city_catalog_close(city_catalog_view_t *v)
{
    if (!v || !v->open) return false;
    if (v->store && !city_content_release(v->store, v->pack)) return false;
    memset(v, 0, sizeof(*v)); return true;
}
static bool compiled(uint32_t index, city_pack_species_t *out)
{
    if (index >= CITY_CATALOG_DEFAULT.count) return false;
    const city_species_definition_t *d = CITY_CATALOG_DEFAULT.at((uint8_t)index);
    if (!d || strlen(d->name) >= sizeof(out->name_en) ||
        strlen(d->description) >= sizeof(out->description_en)) return false;
    city_pack_species_t row = {.species_id = d->species_id, .evolves_from = d->evolves_from,
        .flags = (d->wild_eligible ? 1 : 0) | (d->place_eligible ? 2 : 0) | (d->species_id >= 60000 ? 4 : 0),
        .pool = d->place_pool, .type1 = d->type1, .type2 = d->type2,
        .hp = d->base_hp, .attack = d->base_attack, .defense = d->base_defense};
    strcpy(row.name_en, d->name); strcpy(row.name_zh, d->name);
    strcpy(row.description_en, d->description); strcpy(row.description_zh, d->description);
    *out = row; return true;
}
static void touch_row(city_catalog_view_t *v, unsigned selected)
{
    unsigned old = v->rows[selected].valid ? v->rows[selected].age : CITY_CATALOG_ROWS;
    for (unsigned i = 0; i < CITY_CATALOG_ROWS; ++i)
        if (i != selected && v->rows[i].valid && v->rows[i].age < old) ++v->rows[i].age;
    v->rows[selected].age = 0; v->rows[selected].valid = true;
}
static void cache_row(city_catalog_view_t *v, uint32_t index, const city_pack_species_t *row)
{
    unsigned selected = 0;
    for (unsigned i = 0; i < CITY_CATALOG_ROWS; ++i) {
        if (!v->rows[i].valid || v->rows[i].row.species_id == row->species_id) { selected = i; break; }
        if (v->rows[i].age > v->rows[selected].age) selected = i;
    }
    touch_row(v, selected); v->rows[selected].row = *row; v->rows[selected].index = index;
}
bool city_catalog_at(city_catalog_view_t *v, uint32_t index, city_pack_species_t *out)
{
    if (!live(v) || !out || index >= v->count) return false;
    for (unsigned i = 0; i < CITY_CATALOG_ROWS; ++i) if (v->rows[i].valid && v->rows[i].index == index) {
        touch_row(v, i); *out = v->rows[i].row; return true;
    }
    city_pack_species_t row;
    if (!(v->pack ? city_pack_at(v->pack, index, &row) : compiled(index, &row))) return false;
    cache_row(v, index, &row); *out = row; return true;
}
bool city_catalog_find(city_catalog_view_t *v, uint16_t id, city_pack_species_t *out)
{
    if (!live(v) || !out) return false;
    for (unsigned i = 0; i < CITY_CATALOG_ROWS; ++i) if (v->rows[i].valid && v->rows[i].row.species_id == id) {
        touch_row(v, i); *out = v->rows[i].row; return true;
    }
    if (!v->pack) {
        for (uint32_t i = 0; i < v->count; ++i)
            if (CITY_CATALOG_DEFAULT.at((uint8_t)i)->species_id == id) return city_catalog_at(v, i, out);
        return false;
    }
    city_pack_species_t row;
    if (!city_pack_find(v->pack, id, &row)) return false;
    cache_row(v, UINT32_MAX, &row); *out = row; return true;
}
bool city_catalog_page(city_catalog_view_t *v, uint32_t page, city_catalog_page_t *out)
{
    if (!live(v) || !out || page > v->count / CITY_CATALOG_ROWS) return false;
    city_catalog_page_t next = {.start = page * CITY_CATALOG_ROWS, .total = v->count};
    uint32_t left = v->count - next.start;
    next.count = (uint8_t)(left > CITY_CATALOG_ROWS ? CITY_CATALOG_ROWS : left);
    for (unsigned i = 0; i < next.count; ++i)
        if (!city_catalog_at(v, next.start + i, &next.rows[i])) return false;
    *out = next; return true;
}
static void touch_block(city_catalog_view_t *v, unsigned selected)
{
    unsigned old = v->blocks[selected].valid ? v->blocks[selected].age : CITY_CATALOG_BLOCKS;
    for (unsigned i = 0; i < CITY_CATALOG_BLOCKS; ++i)
        if (i != selected && v->blocks[i].valid && v->blocks[i].age < old) ++v->blocks[i].age;
    v->blocks[selected].age = 0; v->blocks[selected].valid = true;
}
static bool asset(city_catalog_view_t *v, uint16_t id, uint8_t kind, city_pack_asset_t *out)
{
    for (unsigned i = 0; i < CITY_CATALOG_BLOCKS; ++i)
        if (v->blocks[i].valid && v->blocks[i].species_id == id && v->blocks[i].kind == kind) {
            *out = v->blocks[i].asset; return true;
        }
    return city_pack_asset(v->pack, id, kind, out);
}
static const city_catalog_cached_block_t *block(city_catalog_view_t *v, uint16_t id, uint8_t kind,
                                               uint32_t start, const city_pack_asset_t *a)
{
    unsigned selected = 0;
    bool empty = false;
    for (unsigned i = 0; i < CITY_CATALOG_BLOCKS; ++i) {
        if (v->blocks[i].valid && v->blocks[i].species_id == id && v->blocks[i].kind == kind &&
            v->blocks[i].block == start) { touch_block(v, i); return &v->blocks[i]; }
        if (!v->blocks[i].valid) { selected = i; empty = true; }
        else if (!empty && v->blocks[i].age > v->blocks[selected].age) selected = i;
    }
    uint8_t bytes[CITY_PACK_READ_BYTES];
    uint32_t count = a->length - start;
    if (count > sizeof(bytes)) count = sizeof(bytes);
    if (!v->pack->read(v->pack->context, a->offset + start, bytes, count)) return NULL;
    touch_block(v, selected);
    v->blocks[selected].asset = *a; v->blocks[selected].block = start;
    v->blocks[selected].species_id = id; v->blocks[selected].kind = kind;
    v->blocks[selected].bytes = (uint16_t)count;
    memcpy(v->blocks[selected].data, bytes, count); return &v->blocks[selected];
}
bool city_catalog_asset_read(city_catalog_view_t *v, uint16_t id, uint8_t kind,
                             uint32_t offset, void *out, size_t bytes)
{
    if (!live(v) || !v->pack || !out || !bytes || bytes > CITY_PACK_READ_BYTES ||
        (kind != 2 && kind != 3)) return false;
    city_pack_asset_t a;
    if (!asset(v, id, kind, &a) || offset > a.length || bytes > a.length - offset) return false;
    uint8_t next[CITY_PACK_READ_BYTES];
    for (size_t done = 0; done < bytes;) {
        uint32_t position = offset + (uint32_t)done, start = position / CITY_PACK_READ_BYTES * CITY_PACK_READ_BYTES;
        const city_catalog_cached_block_t *b = block(v, id, kind, start, &a);
        if (!b) return false;
        size_t skip = position - start, count = b->bytes - skip;
        if (count > bytes - done) count = bytes - done;
        memcpy(next + done, b->data + skip, count); done += count;
    }
    memcpy(out, next, bytes); return true;
}
