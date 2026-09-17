static bool persist_fixture(const city_bestiary_t *next, void *context)
{ (void)context; return city_bestiary_is_valid(next); }
static void check_save_compatibility(const city_pack_crypto_t *crypto, source_t packs[6])
{
    city_bestiary_t save;
    city_bestiary_init(&save);
    city_content_policy_t policy = {&save, city_content_accept_legacy_save};
    flash_t f; memset(&f, 0xff, sizeof(f)); clear_fault(&f);
    city_content_store_t store; city_content_io_t backend = io(&f);
    assert(!city_content_boot(&store, &backend, crypto, NULL)); assert(!f.calls);
    assert(city_content_boot(&store, &backend, crypto, &policy));
    assert(city_content_install(&store, source_read, &packs[0], packs[0].size));
    assert(city_bestiary_capture(&save, 1, CITY_SPECIES_PIKACHU, 1, persist_fixture, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_content_install(&store, source_read, &packs[1], packs[1].size));
    /* Another captured species makes the formerly valid rollback incompatible. */
    assert(city_bestiary_capture(&save, 2, CITY_SPECIES_BULBASAUR, 2, persist_fixture, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_capture(&save, 3, CITY_SPECIES_BULBASAUR, 3, persist_fixture, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_choose_buddy_instance(&save, 3, persist_fixture, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_damage_instance(&save, 3, 2, persist_fixture, NULL) == CITY_BESTIARY_APPLIED);
    city_bestiary_t snapshot = save;
    flash_t baseline = f;
    assert(!city_content_rollback(&store));
    assert(!memcmp(f.slots, baseline.slots, sizeof(f.slots)));
    assert(!memcmp(f.records, baseline.records, sizeof(f.records)));
    for (unsigned i = 2; i < 6; ++i) {
        int before = f.calls;
        assert(!city_content_install(&store, source_read, &packs[i], packs[i].size));
        assert(f.calls == before); /* Reject incompatible signed source before flash. */
        assert(!memcmp(&save, &snapshot, sizeof(save)));
    }
    city_catalog_view_t view = {0};
    assert(city_catalog_open_installed(&view, &store));
    city_pack_species_t row; assert(city_catalog_find(&view, CITY_SPECIES_BULBASAUR, &row));
    assert(row.hp == 45 && !strcmp(row.name_en, "Bulbasaur"));
    assert(city_catalog_find(&view, 60000, &row));
    assert(city_catalog_close(&view));
    clear_fault(&f); assert(city_content_boot(&store, &backend, crypto, &policy));
    assert(store.active.revision == 2);
    f.slots[1][packs[1].size - 1] ^= 1; clear_fault(&f);
    assert(city_content_boot(&store, &backend, crypto, &policy));
    assert(store.active_slot == -1); /* Old pack rejected; schema-12 compiled fallback safe. */
    save.buddy_instance_id = UINT32_MAX; clear_fault(&f);
    assert(!city_content_boot(&store, &backend, crypto, &policy)); assert(!store.ready);
    assert(!city_content_acquire(&store));
    save = snapshot;
    assert(!memcmp(&save, &snapshot, sizeof(save)));
    puts("Signed pack -> catalog/cache -> save policy: owned copies, buddy, stats, evolution, rollback and fallback passed");
}
