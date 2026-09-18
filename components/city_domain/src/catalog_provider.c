#include "catalog_provider.h"
#include <stddef.h>
#include "species_catalog.inc"

static const city_species_definition_t *compiled_at(uint8_t index)
{
    return index < CITY_SPECIES_COUNT ? &species_definitions[index] : NULL;
}
const city_catalog_provider_t CITY_CATALOG_DEFAULT = {
    .revision = CITY_CATALOG_VERSION,
    .count = CITY_SPECIES_COUNT,
    .at = compiled_at,
};

const city_species_definition_t *city_species_definition(uint16_t species_id)
{
    for (size_t i = 0U; i < CITY_SPECIES_COUNT; ++i) {
        if (CITY_CATALOG_DEFAULT.at(i)->species_id == species_id) {
            return CITY_CATALOG_DEFAULT.at(i);
        }
    }
    return NULL;
}

uint16_t city_species_id_at(uint8_t index)
{
    return index < CITY_SPECIES_COUNT ? CITY_CATALOG_DEFAULT.at(index)->species_id : UINT16_MAX;
}
uint8_t city_species_index(uint16_t species_id)
{
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i)
        if (CITY_CATALOG_DEFAULT.at(i)->species_id == species_id) return i;
    return CITY_SPECIES_COUNT;
}

static city_runtime_catalog_t runtime;
bool city_catalog_bind(const city_runtime_catalog_t *provider)
{
    if (provider && (!provider->count || provider->count > 10000U || !provider->at || !provider->find)) return false;
    runtime = provider ? *provider : (city_runtime_catalog_t){0};
    return true;
}
uint32_t city_species_count(void) { return runtime.count ? runtime.count : CITY_SPECIES_COUNT; }
bool city_species_get(uint16_t id, city_species_definition_t *out)
{
    if (!out) return false;
    if (runtime.count) return runtime.find(runtime.context, id, out);
    const city_species_definition_t *d = city_species_definition(id);
    if (!d) return false;
    *out = *d; return true;
}
bool city_species_get_at(uint32_t index, city_species_definition_t *out)
{
    if (!out || index >= city_species_count()) return false;
    if (runtime.count) return runtime.at(runtime.context, index, out);
    *out = *CITY_CATALOG_DEFAULT.at((uint8_t)index); return true;
}
uint32_t city_species_position(uint16_t id)
{
    /* Package rows are stable-ID sorted; compiled order stays legacy-compatible. */
    if (!runtime.count) return city_species_index(id);
    uint32_t lo = 0, hi = runtime.count;
    while (lo < hi) {
        uint32_t mid = lo + (hi-lo)/2; city_species_definition_t d;
        if (!city_species_get_at(mid, &d)) return runtime.count;
        if (d.species_id < id) lo = mid+1; else hi = mid;
    }
    city_species_definition_t d;
    return lo < runtime.count && city_species_get_at(lo, &d) && d.species_id == id ? lo : runtime.count;
}
uint16_t city_species_runtime_id(uint32_t index)
{
    city_species_definition_t d;
    return city_species_get_at(index, &d) ? d.species_id : UINT16_MAX;
}

static void traits(const city_pack_species_t *row, city_species_definition_t *d)
{
    *d = (city_species_definition_t){.species_id=row->species_id, .type1=row->type1,
        .type2=row->type2, .base_hp=row->hp, .base_attack=row->attack, .base_defense=row->defense,
        .place_pool=row->pool, .wild_eligible=(row->flags & 1)!=0, .place_eligible=(row->flags & 2)!=0,
        .evolves_from=row->evolves_from};
}
static bool pack_at(void *context, uint32_t index, city_species_definition_t *out)
{
    city_pack_species_t row;
    if (!city_pack_at(context, index, &row)) return false;
    traits(&row, out); return true;
}
static bool pack_find(void *context, uint16_t id, city_species_definition_t *out)
{
    city_pack_species_t row;
    if (!city_pack_find(context, id, &row)) return false;
    traits(&row, out); return true;
}
bool city_catalog_bind_pack(const city_pack_t *pack)
{
    if (!pack) return city_catalog_bind(NULL);
    if (!pack->verified) return false;
    const city_runtime_catalog_t provider = {(void *)pack, pack->count, pack_at, pack_find};
    return city_catalog_bind(&provider);
}
