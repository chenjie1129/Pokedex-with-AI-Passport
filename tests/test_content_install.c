#include "content_install.h"
#include "content_compat.h"
#include "catalog_view.h"
#include "bsp_content_crypto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAPACITY 4096U
typedef struct { uint8_t *data; uint32_t size; } source_t;
typedef struct {
    uint8_t slots[2][CAPACITY], records[2][CITY_CONTENT_RECORD_BYTES];
    int calls, fail_call, fault_kind, cut;
    bool stopped, silent_corruption;
} flash_t;
static bool io_ok(flash_t *f)
{
    ++f->calls;
    if (f->fail_call == f->calls) f->stopped = true;
    return !f->stopped;
}
static bool source_read(void *arg, uint32_t offset, void *out, size_t n)
{
    source_t *s = arg;
    if (offset > s->size || n > s->size - offset) return false;
    memcpy(out, s->data + offset, n); return true;
}
static bool read_slot(void *arg, unsigned slot, uint32_t offset, void *out, size_t n)
{
    flash_t *f = arg;
    assert(slot < 2 && offset <= CAPACITY && n <= CAPACITY - offset);
    if (!io_ok(f)) return false;
    memcpy(out, f->slots[slot] + offset, n); return true;
}
static bool mutate(flash_t *f, int kind, uint8_t *dest, const uint8_t *src, size_t n)
{
    if (!io_ok(f)) return false;
    bool torn = f->fault_kind == kind;
    /* Carry the cut across multiple payload write chunks. */
    if (torn && kind == 2 && (size_t)f->cut > n) {
        f->cut -= (int)n; torn = false;
    }
    size_t limit = torn && (size_t)f->cut < n ? (size_t)f->cut : n;
    for (size_t i = 0; i < limit; ++i) {
        if (src) { assert((dest[i] & src[i]) == src[i]); dest[i] &= src[i]; }
        else dest[i] = 0xff;
    }
    if (f->silent_corruption && kind == 2 && n) dest[n - 1] ^= 1;
    if (torn) f->stopped = true;
    return !torn;
}
static bool erase_slot(void *arg, unsigned slot)
{ flash_t *f = arg; assert(slot < 2); return mutate(f, 1, f->slots[slot], NULL, CAPACITY); }
static bool write_slot(void *arg, unsigned slot, uint32_t offset, const void *src, size_t n)
{
    flash_t *f = arg; assert(slot < 2 && offset <= CAPACITY && n <= CAPACITY - offset);
    return mutate(f, 2, f->slots[slot] + offset, src, n);
}
static bool read_record(void *arg, unsigned bank, uint8_t *out)
{
    flash_t *f = arg; assert(bank < 2);
    if (!io_ok(f)) return false;
    memcpy(out, f->records[bank], CITY_CONTENT_RECORD_BYTES); return true;
}
static bool erase_record(void *arg, unsigned bank)
{ flash_t *f = arg; assert(bank < 2); return mutate(f, 3, f->records[bank], NULL, CITY_CONTENT_RECORD_BYTES); }
static bool write_record(void *arg, unsigned bank, const uint8_t *src)
{ flash_t *f = arg; assert(bank < 2); return mutate(f, 4, f->records[bank], src, CITY_CONTENT_RECORD_BYTES); }
static void clear_fault(flash_t *f)
{ f->calls = 0; f->fail_call = 0; f->fault_kind = 0; f->cut = 0; f->stopped = false; f->silent_corruption = false; }
static city_content_io_t io(flash_t *f)
{ return (city_content_io_t){f, CAPACITY, read_slot, erase_slot, write_slot, read_record, erase_record, write_record}; }
static source_t load(const char *path)
{
    FILE *f = fopen(path, "rb"); assert(f);
    assert(fseek(f, 0, SEEK_END) == 0); long size = ftell(f); assert(size >= 0 && size <= CAPACITY);
    rewind(f); source_t s = {malloc(size ? (size_t)size : 1), (uint32_t)size}; assert(s.data);
    assert(fread(s.data, 1, s.size, f) == s.size); fclose(f); return s;
}
/* Storage-only fixture. Save compatibility has separate real-policy tests. */
static bool storage_fixture_policy(void *context, const city_pack_t *candidate)
{ (void)context; return !candidate || candidate->verified; }
static const city_content_policy_t storage_policy = {NULL, storage_fixture_policy};
static void restart(city_content_store_t *s, flash_t *f, const city_pack_crypto_t *c)
{ clear_fault(f); city_content_io_t backend = io(f); assert(city_content_boot(s, &backend, c, &storage_policy)); }
static void active(city_content_store_t *s, uint32_t revision)
{
    const city_pack_t *p = city_content_acquire(s); assert(p && p->revision == revision);
    city_pack_species_t row; assert(city_pack_at(p, 0, &row));
    assert(row.species_id == 1); assert(city_content_release(s, p));
}
#include "content_compat_checks.h"

int main(int argc, char **argv)
{
    assert(argc == 3 || argc == 5 || argc == 8);
    source_t key = load(argv[1]), first = load(argv[2]);
    bsp_content_crypto_t backend;
    bool valid_key = bsp_content_crypto_init(&backend, key.data, key.size);
    city_pack_crypto_t crypto = bsp_content_crypto_callbacks(&backend);
    city_pack_t p;
    bool valid = valid_key && city_pack_open(&p, source_read, &first, first.size, CAPACITY, &crypto);
    if (argc == 3) {
        bsp_content_crypto_free(&backend); free(key.data); free(first.data);
        printf("%s\n", valid ? "verified" : "rejected"); return valid ? 0 : 1;
    }
    assert(valid);
    if (argc == 8) {
        source_t packs[6] = {first};
        for (unsigned i = 1; i < 6; ++i) packs[i] = load(argv[i + 2]);
        check_save_compatibility(&crypto, packs);
        for (unsigned i = 0; i < 6; ++i) free(packs[i].data);
        free(key.data); bsp_content_crypto_free(&backend); return 0;
    }
    source_t second = load(argv[3]), third = load(argv[4]);
    flash_t f; memset(&f, 0xff, sizeof(f)); clear_fault(&f);
    city_content_store_t store; restart(&store, &f, &crypto);
    assert(store.active_slot == -1 && !city_content_acquire(&store));
    assert(!city_content_rollback(&store));
    assert(city_content_install(&store, source_read, &first, first.size)); active(&store, 1);
    const city_pack_t *lease = city_content_acquire(&store); assert(lease);
    assert(!city_content_install(&store, source_read, &second, second.size));
    assert(!city_content_rollback(&store)); assert(city_content_release(&store, lease));
    assert(!city_content_release(&store, lease));
    assert(city_content_install(&store, source_read, &second, second.size)); active(&store, 2);
    flash_t baseline = f;
    assert(city_content_rollback(&store)); active(&store, 1);
    restart(&store, &f, &crypto); active(&store, 1); assert(store.high_revision == 2);
    int before = f.calls;
    assert(!city_content_install(&store, source_read, &second, second.size)); assert(f.calls == before);
    assert(city_content_rollback(&store)); active(&store, 2);
    /* Real P-256 and typed validation must reject before any flash I/O. */
    third.data[third.size - 1] ^= 1; before = f.calls;
    assert(!city_content_install(&store, source_read, &third, third.size)); assert(f.calls == before);
    third.data[third.size - 1] ^= 1;
    f = baseline; restart(&store, &f, &crypto); f.calls = 0;
    assert(city_content_install(&store, source_read, &third, third.size));
    int operations = f.calls;
    for (int fail = 1; fail <= operations; ++fail) {
        f = baseline; restart(&store, &f, &crypto); f.calls = 0; f.fail_call = fail;
        assert(!city_content_install(&store, source_read, &third, third.size));
        assert(!memcmp(f.slots[1], baseline.slots[1], CAPACITY));
        restart(&store, &f, &crypto);
        assert(store.active.revision == 2 || store.active.revision == 3);
        active(&store, store.active.revision);
    }
    unsigned cuts = 0;
    for (int kind = 1; kind <= 4; ++kind) {
        int limit = kind <= 2 ? (int)third.size : (int)CITY_CONTENT_RECORD_BYTES;
        for (int cut = 0; cut <= limit; ++cut) {
            f = baseline; restart(&store, &f, &crypto); f.fault_kind = kind; f.cut = cut;
            assert(!city_content_install(&store, source_read, &third, third.size));
            assert(!memcmp(f.slots[1], baseline.slots[1], CAPACITY));
            if (kind >= 3) assert(!store.ready && !city_content_acquire(&store));
            restart(&store, &f, &crypto);
            assert(store.active.revision == 2 || store.active.revision == 3);
            active(&store, store.active.revision); ++cuts;
        }
    }
    /* Torn rollback journal never drops both packs; it chooses either revision. */
    for (int kind = 3; kind <= 4; ++kind) for (int cut = 0; cut <= (int)CITY_CONTENT_RECORD_BYTES; ++cut) {
        f = baseline; restart(&store, &f, &crypto); f.fault_kind = kind; f.cut = cut;
        assert(!city_content_rollback(&store)); restart(&store, &f, &crypto);
        assert(store.active.revision == 1 || store.active.revision == 2); active(&store, store.active.revision);
        assert(store.high_revision == 2); ++cuts;
    }
    f = baseline; restart(&store, &f, &crypto); f.silent_corruption = true;
    assert(!city_content_install(&store, source_read, &third, third.size));
    assert(!memcmp(f.records, baseline.records, sizeof(f.records)));
    restart(&store, &f, &crypto); active(&store, 2);
    for (int fail = 1; fail <= 2; ++fail) {
        f = baseline; clear_fault(&f); f.fail_call = fail; city_content_io_t broken = io(&f);
        assert(!city_content_boot(&store, &broken, &crypto, &storage_policy)); assert(!store.ready);
    }
    f = baseline; f.slots[1][second.size - 1] ^= 1;
    restart(&store, &f, &crypto); active(&store, 1); assert(store.high_revision == 2);
    f.slots[0][first.size - 1] ^= 1;
    restart(&store, &f, &crypto); assert(store.active_slot == -1);
    f = baseline; f.records[1][0] ^= 1;
    restart(&store, &f, &crypto); active(&store, 1);
    store.generation = UINT32_MAX; before = f.calls;
    assert(!city_content_install(&store, source_read, &third, third.size));
    assert(!city_content_rollback(&store)); assert(f.calls == before);
    printf("P-256 adapter + installer: %d I/O failures, %u torn mutations, leases, rollback, reboot passed\n", operations, cuts);
    bsp_content_crypto_free(&backend); free(key.data); free(first.data); free(second.data); free(third.data);
    return 0;
}
