/* Included by the real-crypto reader fixture after its context/read callbacks. */
static void check_catalog_view(context_t *c, const city_pack_t *pack)
{
    city_catalog_view_t view = {0};
    assert(sizeof(view) <= 4096); /* Fixed budget, independent of catalog count. */
    assert(city_catalog_open_compiled(&view));
    city_pack_species_t row;
    for (uint32_t i = 0; i < view.count; ++i) {
        assert(city_catalog_at(&view, i, &row));
        const city_species_definition_t *d = CITY_CATALOG_DEFAULT.at((uint8_t)i);
        assert(row.species_id == d->species_id && row.hp == d->base_hp);
        assert(row.type1 && !strcmp(row.name_en, d->name));
    }
    assert(city_catalog_close(&view));
    /* The reader has already authenticated this immutable file. This fixture
     * supplies that verified handle; installer/boot are exercised separately. */
    city_content_store_t store = {.active = *pack, .active_slot = 0, .ready = true};
    assert(city_catalog_open_installed(&view, &store));
    assert(store.leases == 1 && view.count == pack->count);
    assert(!city_catalog_open_compiled(&view));
    assert(!city_content_install(&store, read_bytes, c, pack->bytes));
    assert(!city_content_rollback(&store));
    city_catalog_page_t page, saved;
    for (uint32_t i = 0; i <= view.count / 4; ++i) {
        assert(city_catalog_page(&view, i, &page));
        assert(page.count <= 4 && page.total == view.count && page.start == i * 4);
        for (unsigned j = 0; j < page.count; ++j) {
            assert(city_catalog_find(&view, page.rows[j].species_id, &row));
            assert(!memcmp(&row, &page.rows[j], sizeof(row)));
        }
    }
    saved = page;
    assert(!city_catalog_page(&view, UINT32_MAX, &page)); assert(!memcmp(&saved, &page, sizeof(page)));
    /* Touching row zero protects it while the least-recent row is evicted. */
    if (view.count >= 5) {
        for (unsigned i = 0; i < 4; ++i) assert(city_catalog_at(&view, i, &row));
        unsigned reads = c->reads;
        assert(city_catalog_at(&view, 0, &row)); assert(c->reads == reads);
        city_pack_species_t retained = row;
        assert(city_catalog_at(&view, 4, &row)); reads = c->reads;
        assert(city_catalog_at(&view, 0, &row)); assert(c->reads == reads);
        assert(city_catalog_at(&view, 1, &row)); assert(c->reads > reads);
        assert(retained.species_id != row.species_id); /* Caller copy survives eviction. */
    }
    assert(city_catalog_at(&view, 0, &row));
    city_pack_asset_t a; assert(city_pack_asset(pack, row.species_id, 2, &a));
    uint8_t output[512], expected[512], before[512];
    memset(output, 0xa5, sizeof(output)); memcpy(before, output, sizeof(output));
    assert(!city_catalog_asset_read(&view, row.species_id, 2, a.length, output, 1));
    assert(!memcmp(output, before, sizeof(output)));
    assert(!city_catalog_asset_read(&view, row.species_id, 1, 0, output, 1));
    assert(city_catalog_asset_read(&view, row.species_id, 2, 0, output, 12));
    unsigned reads = c->reads;
    assert(city_catalog_asset_read(&view, row.species_id, 2, 0, output, 12)); assert(c->reads == reads);
    if (a.length > 5 * 512) {
        /* Fill all four blocks, protect block zero, and evict block one. */
        for (unsigned i = 0; i < 4; ++i)
            assert(city_catalog_asset_read(&view, row.species_id, 2, i * 512, output, 12));
        assert(city_catalog_asset_read(&view, row.species_id, 2, 0, output, 12));
        assert(city_catalog_asset_read(&view, row.species_id, 2, 4 * 512, output, 12));
        reads = c->reads;
        assert(city_catalog_asset_read(&view, row.species_id, 2, 0, output, 12)); assert(c->reads == reads);
        assert(city_catalog_asset_read(&view, row.species_id, 2, 512, output, 12)); assert(c->reads > reads);
        assert(city_pack_read_asset(pack, row.species_id, 2, 500, expected, 24));
        assert(city_catalog_close(&view)); assert(city_catalog_open_installed(&view, &store));
        memcpy(output, before, sizeof(output)); c->fail_offset = a.offset + 512;
        assert(!city_catalog_asset_read(&view, row.species_id, 2, 500, output, 24));
        assert(!memcmp(output, before, sizeof(output))); c->fail_offset = UINT32_MAX;
        assert(city_catalog_asset_read(&view, row.species_id, 2, 500, output, 24));
        assert(!memcmp(output, expected, 24));
    }
    assert(city_catalog_close(&view)); assert(store.leases == 0);
    assert(!city_catalog_at(&view, 0, &row));
    assert(city_catalog_open_installed(&view, &store));
    memset(&page, 0xa5, sizeof(page)); saved = page; c->fault = "read";
    assert(!city_catalog_page(&view, 0, &page)); assert(!memcmp(&page, &saved, sizeof(page)));
    c->fault = ""; assert(city_catalog_close(&view));
    assert(city_catalog_open_compiled(&view));
    assert(city_catalog_at(&view, 0, &row)); assert(!strcmp(row.name_en, "Bulbasaur"));
    assert(city_catalog_close(&view));
}
