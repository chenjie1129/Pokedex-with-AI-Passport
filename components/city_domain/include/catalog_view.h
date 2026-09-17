#pragma once
#include "content_install.h"
#include "catalog_provider.h"
#define CITY_CATALOG_ROWS 4U
#define CITY_CATALOG_BLOCKS 4U

typedef struct {
    city_pack_species_t row;
    uint32_t index;
    uint8_t age;
    bool valid;
} city_catalog_cached_row_t;
typedef struct {
    city_pack_asset_t asset;
    uint32_t block;
    uint16_t species_id, bytes;
    uint8_t kind, age;
    bool valid;
    uint8_t data[CITY_PACK_READ_BYTES];
} city_catalog_cached_block_t;
/* Zero-initialize once. Serialized worker owns the view at a stable address.
 * Open/close require no concurrent consumers. Outputs are copies; no cache
 * pointers escape. The package lease lasts until close, blocking replacement.
 * Never use pack-backed reads on LVGL/audio callback threads. */
typedef struct {
    city_content_store_t *store;
    const city_pack_t *pack;
    uint32_t count, revision;
    bool open;
    city_catalog_cached_row_t rows[CITY_CATALOG_ROWS];
    city_catalog_cached_block_t blocks[CITY_CATALOG_BLOCKS];
} city_catalog_view_t;
typedef struct {
    uint32_t start, total;
    uint8_t count;
    city_pack_species_t rows[CITY_CATALOG_ROWS];
} city_catalog_page_t;

/* Existing compiled order is preserved. Chinese fields carry English fallback;
 * the current UI's ui_strings adapter remains responsible for compiled locale. */
bool city_catalog_open_compiled(city_catalog_view_t *);
bool city_catalog_open_installed(city_catalog_view_t *, city_content_store_t *);
bool city_catalog_close(city_catalog_view_t *);
bool city_catalog_at(city_catalog_view_t *, uint32_t index, city_pack_species_t *);
bool city_catalog_find(city_catalog_view_t *, uint16_t id, city_pack_species_t *);
/* Page number is zero-based. An empty page at count/4 is allowed only when it
 * starts exactly at count (the UI can place its Back row there). */
bool city_catalog_page(city_catalog_view_t *, uint32_t page, city_catalog_page_t *);
/* Pack assets only; offsets include their typed 12-byte header. Existing compiled
 * sprites/cries retain their static adapters. All failed reads leave out intact. */
bool city_catalog_asset_read(city_catalog_view_t *, uint16_t id, uint8_t kind,
                             uint32_t offset, void *out, size_t bytes);
