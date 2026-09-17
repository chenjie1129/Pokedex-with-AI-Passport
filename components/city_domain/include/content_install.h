#pragma once
#include "content_pack.h"
#define CITY_CONTENT_RECORD_BYTES 80U

/* Synchronous worker-only API. Manager/context addresses stay stable; boot only
 * before readers are handed out. Caller serializes all operations, including leases.
 * Backends erase only the indicated inactive slot or one selector bank; successful
 * writes must be durable. Never expose backend mutation elsewhere. */
typedef struct {
    void *context;
    uint32_t capacity;
    bool (*read)(void *, unsigned slot, uint32_t offset, void *, size_t);
    bool (*erase)(void *, unsigned slot);
    bool (*write)(void *, unsigned slot, uint32_t offset, const void *, size_t);
    bool (*read_record)(void *, unsigned bank, uint8_t out[CITY_CONTENT_RECORD_BYTES]);
    bool (*erase_record)(void *, unsigned bank);
    bool (*write_record)(void *, unsigned bank, const uint8_t bytes[CITY_CONTENT_RECORD_BYTES]);
} city_content_io_t;
struct city_content_store;
typedef struct { struct city_content_store *store; unsigned slot; } city_content_slot_t;
typedef struct city_content_store {
    city_content_io_t io;
    city_pack_crypto_t crypto;
    city_content_slot_t slots[2];
    city_pack_t active;
    uint32_t generation, high_revision, leases;
    int active_slot, active_bank;
    bool ready;
} city_content_store_t;

/* No valid installed pack -> success with active_slot=-1 (compiled fallback).
 * I/O errors -> false. Corrupt records/packs never become active. */
bool city_content_boot(city_content_store_t *, const city_content_io_t *, const city_pack_crypto_t *);
/* Copies into inactive slot, verifies readback, then journals activation. Revision
 * must exceed all surviving valid records. A false result after journal I/O makes
 * ready=false: reboot/reconcile before retry because commit can be ambiguous. */
bool city_content_install(city_content_store_t *, city_pack_read_fn source, void *, uint32_t bytes);
/* Explicitly selects the previous still-valid pack; retains revision high water.
 * Staging the next update consumes the old rollback slot. */
bool city_content_rollback(city_content_store_t *);
/* The returned handle is immutable until balanced release; installs and rollback
 * reject outstanding leases. Do not retain handles or assets after release. */
const city_pack_t *city_content_acquire(city_content_store_t *);
bool city_content_release(city_content_store_t *, const city_pack_t *);
