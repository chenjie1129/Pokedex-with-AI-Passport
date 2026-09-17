#include "content_compat.h"

static bool matches(const city_pack_t *pack, uint16_t id)
{
    const city_species_definition_t *d = city_species_definition(id);
    city_pack_species_t row;
    return d && city_pack_find(pack, id, &row) && row.hp == d->base_hp &&
        row.attack == d->base_attack && row.defense == d->base_defense &&
        row.type1 == d->type1 && row.type2 == d->type2 && row.evolves_from == d->evolves_from;
}
bool city_content_accept_legacy_save(void *context, const city_pack_t *pack)
{
    const city_bestiary_t *save = context;
    if (!city_bestiary_is_valid(save)) return false;
    if (!pack) return true; /* Current schema was validated against compiled content. */
    if (!pack->verified) return false;
    for (unsigned i = 0; i < CITY_SPECIES_COUNT; ++i) {
        const city_creature_record_t *record = &save->records[i];
        if (record->state == CITY_DISCOVERY_UNKNOWN) continue;
        if (!matches(pack, record->species_id)) return false;
        const city_species_definition_t *d = city_species_definition(record->species_id);
        if (d->evolves_from && !matches(pack, d->evolves_from)) return false;
        if (record->state == CITY_DISCOVERY_CAPTURED) {
            uint16_t target = city_evolution_target(record->species_id);
            if (target && !matches(pack, target)) return false;
        }
    }
    return true;
}
