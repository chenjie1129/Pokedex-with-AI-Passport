#include "content_runtime.h"
#include "catalog_provider.h"
#include <stdlib.h>
static bool same_traits(const city_pack_species_t *a, const city_pack_species_t *b)
{
    return a->hp==b->hp && a->attack==b->attack && a->defense==b->defense &&
        a->type1==b->type1 && a->type2==b->type2 && a->evolves_from==b->evolves_from;
}
bool city_content_accept_runtime(void *context, const city_pack_t *pack)
{
    const city_content_runtime_policy_t *policy=context;
    if (!policy || !policy->text_compatible || (policy->save_bytes && !policy->save)) return false;
    if (pack) {
        /* Preserve the built-in fallback's mechanics and every legacy identity. */
        for (unsigned i=0;i<CITY_SPECIES_COUNT;++i) {
            const city_species_definition_t *d=CITY_CATALOG_DEFAULT.at(i);
            city_pack_species_t r;
            if (!city_pack_find(pack,d->species_id,&r) || r.hp!=d->base_hp ||
                r.attack!=d->base_attack || r.defense!=d->base_defense ||
                r.type1!=d->type1 || r.type2!=d->type2 || r.evolves_from!=d->evolves_from) return false;
        }
        for (uint32_t i=0;i<pack->count;++i) {
            city_pack_species_t r;
            if (!city_pack_at(pack,i,&r) || !policy->text_compatible(&r)) return false;
        }
    }
    if (!city_catalog_bind_pack(pack)) return false;
    city_bestiary_t *save=malloc(sizeof(*save));
    bool ok=save && (!policy->save || city_bestiary_decode(policy->save,policy->save_bytes,save));
    /* Approved updates must retain traits of all discovered package species.
     * On boot, only authenticated journaled revisions are candidates. */
    if (ok && policy->save && policy->previous && pack) {
        for (unsigned i=0;i<CITY_MAX_PROGRESS_RECORDS;++i) {
            const city_creature_record_t *r=&save->records[i];
            if (r->state==CITY_DISCOVERY_UNKNOWN) continue;
            city_pack_species_t a,b;
            if (!city_pack_find(policy->previous,r->species_id,&a) ||
                !city_pack_find(pack,r->species_id,&b) || !same_traits(&a,&b)) { ok=false; break; }
            /* Retain any available evolution, even before the player earns it. */
            for (uint32_t j=0;j<policy->previous->count;++j) {
                if (!city_pack_at(policy->previous,j,&a)) { ok=false; break; }
                if (a.evolves_from==r->species_id &&
                    (!city_pack_find(pack,a.species_id,&b) || !same_traits(&a,&b))) { ok=false; break; }
            }
        }
    }
    free(save); city_catalog_bind(NULL); return ok;
}
