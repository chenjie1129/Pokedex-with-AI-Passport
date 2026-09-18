#include "content_runtime.h"
static bool allow_text(const city_pack_species_t *row) { return row && row->name_en[0]; }
static bool reject_text(const city_pack_species_t *row) { (void)row; return false; }
static void check_runtime_content(const city_pack_crypto_t *crypto,source_t source, source_t changed)
{
    city_pack_t pack;
    assert(city_pack_open(&pack,source_read,&source,source.size,source.size,crypto));
    city_content_runtime_policy_t gate={.text_compatible=allow_text};
    assert(city_content_accept_runtime(&gate,&pack));
    assert(city_catalog_bind_pack(&pack));
    city_bestiary_t save;city_bestiary_init(&save);
    assert(city_bestiary_capture(&save,1,60001,1,persist_fixture,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_choose_buddy_instance(&save,1,persist_fixture,NULL)==CITY_BESTIARY_APPLIED);
    uint8_t bytes[CITY_BESTIARY_STORAGE_MAX_BYTES];size_t length;
    assert(city_bestiary_encode_sparse(&save,bytes,sizeof(bytes),&length));
    gate.save=bytes;gate.save_bytes=length;
    assert(city_content_accept_runtime(&gate,&pack));
    assert(!city_content_accept_runtime(&gate,NULL));
    gate.text_compatible=reject_text;assert(!city_content_accept_runtime(&gate,&pack));
    gate.text_compatible=allow_text;gate.previous=&pack;
    assert(city_content_accept_runtime(&gate,&pack));
    city_pack_t different;
    assert(city_pack_open(&different,source_read,&changed,changed.size,changed.size,crypto));
    assert(!city_content_accept_runtime(&gate,&different));
    flash_t f;memset(&f,0xff,sizeof(f));clear_fault(&f);
    city_content_store_t store;city_content_io_t backend=io(&f);
    city_content_policy_t policy={&gate,city_content_accept_runtime};
    /* An empty install cannot hide a save that requires package-only content. */
    assert(!city_content_boot(&store,&backend,crypto,&policy));
    gate.save=NULL;gate.save_bytes=0;gate.previous=NULL;
    assert(city_content_boot(&store,&backend,crypto,&policy));
    assert(city_content_install(&store,source_read,&source,source.size));
    gate.save=bytes;gate.save_bytes=length;
    assert(city_content_boot(&store,&backend,crypto,&policy));
    assert(city_catalog_bind_pack(&store.active));
    city_bestiary_t loaded;assert(city_bestiary_decode(bytes,length,&loaded));
    assert(loaded.owned_count==1 && loaded.owned[0].species_id==60001 && loaded.buddy_instance_id==1);
    assert(city_catalog_bind(NULL));
    puts("Actual signed package -> device crypto -> install -> package capture -> sparse save -> reboot passed");
}
