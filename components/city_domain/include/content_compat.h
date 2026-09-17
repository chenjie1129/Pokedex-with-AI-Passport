#pragma once
#include "bestiary_service.h"
#include "content_pack.h"
/* Policy for the current schema-12 compiled-roster save only. The context is an
 * immutable save snapshot for the duration of a serialized content operation.
 * Preserves discovered IDs, base stats/types, evolution identity and the known
 * next evolution of captured species. NULL candidate checks compiled fallback.
 * Does not migrate saves or authorize captures of package-only species. */
bool city_content_accept_legacy_save(void *save, const city_pack_t *candidate);
