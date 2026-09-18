#include "pokemon_content.h"
#include "catalog_view.h"
#include "content_runtime.h"
#include "bsp_content_storage.h"
#include "bsp_content_crypto.h"
#include "bsp_bestiary_store.h"
#include "content_trust_key.h"
#include "ui_fonts.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/misc/cache/instance/lv_image_header_cache.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "content";
static city_content_store_t store;
static bsp_content_storage_t storage;
static city_catalog_view_t ui_view, audio_view;
static const uint8_t *mapped;
static esp_partition_mmap_handle_t mapping;
static uint8_t *saved_blob;
static size_t saved_bytes;
static city_content_runtime_policy_t policy;
static const char *types[] = {"", "Normal", "Fire", "Water", "Electric", "Grass", "Ice",
    "Fighting", "Poison", "Ground", "Flying", "Psychic", "Bug", "Rock", "Ghost", "Dragon",
    "Dark", "Steel", "Fairy"};

static bool glyphs(const char *text, const lv_font_t *font)
{
    uint32_t at=0;
    while (text[at]) {
        uint32_t code=(uint8_t)text[at++];
        unsigned more=0;
        if (code>=0xf0) { more=3; code&=7; }
        else if (code>=0xe0) { more=2; code&=15; }
        else if (code>=0xc0) { more=1; code&=31; }
        while (more--) code=(code<<6)|((uint8_t)text[at++]&63);
        lv_font_glyph_dsc_t d;
        if (!lv_font_get_glyph_dsc(font,&d,code,0)) return false;
    }
    return true;
}
static bool text_fits(const city_pack_species_t *row)
{
    const char *names[]={row->name_en,row->name_zh};
    const char *descriptions[]={row->description_en,row->description_zh};
    for (unsigned i=0;i<2;++i) {
        char title[80]; snprintf(title,sizeof(title),"No.%03u %s",row->species_id,names[i]);
        if (!glyphs(title,&city_font_20) || !glyphs(descriptions[i],&city_font_14)) return false;
        lv_point_t size;
        lv_text_get_size(&size,title,&city_font_20,0,0,220,LV_TEXT_FLAG_NONE);
        if (size.y>24) return false;
        lv_text_get_size(&size,descriptions[i],&city_font_14,0,0,220,LV_TEXT_FLAG_NONE);
        if (size.y>48) return false;
    }
    return true;
}
static bool read_saved_blob(void)
{
    nvs_handle_t nvs;
    esp_err_t err=nvs_open("pokedex",NVS_READONLY,&nvs);
    if (err==ESP_ERR_NVS_NOT_FOUND) return true;
    if (err!=ESP_OK) return false;
    err=nvs_get_blob(nvs,"bestiary_v6",NULL,&saved_bytes);
    if (err==ESP_ERR_NVS_NOT_FOUND) { nvs_close(nvs); return true; }
    if (err!=ESP_OK || saved_bytes>CITY_BESTIARY_STORAGE_MAX_BYTES || saved_bytes<12) { nvs_close(nvs); return false; }
    saved_blob=malloc(saved_bytes);
    if (!saved_blob) { nvs_close(nvs); return false; }
    err=nvs_get_blob(nvs,"bestiary_v6",saved_blob,&saved_bytes);
    nvs_close(nvs); return err==ESP_OK;
}
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static bool mapped_read(void *arg,uint32_t off,void *out,size_t n)
{
    (void)arg;
    if (!mapped || n>CITY_PACK_READ_BYTES || off>store.active.bytes || n>store.active.bytes-off) return false;
    memcpy(out,mapped+off,n); return true;
}
bool pokemon_content_boot(void)
{
    int64_t started=esp_timer_get_time();
    city_content_io_t io;
    if (!bsp_content_storage_open(&storage,&io)) {
        ESP_LOGI(TAG,"CONTENT_READY source=compiled layout=legacy"); return true;
    }
    if (!read_saved_blob()) { free(saved_blob); saved_blob=NULL; return false; }
    bsp_content_crypto_t crypto;
    bool initialized=bsp_content_crypto_init(&crypto,content_trust_key,sizeof(content_trust_key));
    city_pack_crypto_t callbacks=bsp_content_crypto_callbacks(&crypto);
    policy=(city_content_runtime_policy_t){saved_blob,saved_bytes,NULL,text_fits};
    city_content_policy_t gate={&policy,city_content_accept_runtime};
    bool ok=initialized && city_content_boot(&store,&io,&callbacks,&gate);
    if (ok) for (unsigned slot=0;slot<2;++slot) {
        if (slot==(unsigned)store.active_slot) continue;
        uint8_t header[28];
        if (!io.read(io.context,slot,0,header,sizeof(header))) { ok=false; break; }
        if (memcmp(header,"CITYPK01",8)) continue;
        uint32_t count=u32(header+16), payload=u32(header+20);
        if (count>CITY_PACK_MAX_SPECIES*3U || payload>io.capacity) continue;
        uint32_t bytes=28U+count*44U+64U+payload;
        if (bytes>io.capacity || u32(header+12)<=store.high_revision) continue;
        policy.previous=store.active_slot>=0 ? &store.active : NULL;
        bool activated=city_content_activate_staged(&store,slot,bytes);
        policy.previous=NULL;
        ESP_LOGI(TAG,"CONTENT_STAGED slot=%u accepted=%u",slot,activated);
        if (!store.ready) { ok=false; break; }
    }
    bsp_content_crypto_free(&crypto); free(saved_blob); saved_blob=NULL;
    if (!ok) { ESP_LOGE(TAG,"CONTENT_UNAVAILABLE save_or_journal_rejected"); return false; }
    if (store.active_slot<0) { ESP_LOGI(TAG,"CONTENT_READY source=compiled layout=content"); return true; }
    const void *address=NULL;
    if (esp_partition_mmap(storage.slots[store.active_slot],0,store.active.bytes,
        ESP_PARTITION_MMAP_DATA,&address,&mapping)!=ESP_OK) return false;
    mapped=address;
    /* Immutable mapped reads after all boot mutations finish. No partition I/O,
     * crypto, allocation, or shared cache mutation in domain/UI lookups. */
    store.active.read=mapped_read; store.active.context=NULL;
    if (!city_catalog_bind_pack(&store.active) || !city_catalog_open_installed(&ui_view,&store) ||
        !city_catalog_open_installed(&audio_view,&store)) {
        if (ui_view.open) city_catalog_close(&ui_view);
        if (audio_view.open) city_catalog_close(&audio_view);
        city_catalog_bind(NULL);esp_partition_munmap(mapping);mapped=NULL;return false;
    }
    ESP_LOGI(TAG,"CONTENT_READY source=pack slot=%d revision=%lu species=%lu bytes=%lu boot_ms=%lu heap=%lu stack=%u",
        store.active_slot,(unsigned long)store.active.revision,(unsigned long)store.active.count,
        (unsigned long)store.active.bytes,(unsigned long)((esp_timer_get_time()-started)/1000),
        (unsigned long)esp_get_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(NULL));
    return true;
}
bool pokemon_content_active(void) { return mapped && ui_view.open; }
const city_species_definition_t *pokemon_content_definition(uint16_t id,city_language_t language)
{
    if (!pokemon_content_active()) return city_species_definition(id);
    /* Only the LVGL owner uses this small copy ring. Rendering retains no pointer
     * after building a screen; domain/audio use independent copy APIs. */
    static struct { city_pack_species_t row; city_species_definition_t d; char type[32]; } rows[4];
    static unsigned cursor;
    unsigned i=cursor++%4;
    if (!city_catalog_find(&ui_view,id,&rows[i].row) || !city_species_get(id,&rows[i].d)) return NULL;
    city_pack_species_t *r=&rows[i].row;
    city_species_definition_t *d=&rows[i].d;
    d->name=language==CITY_LANGUAGE_SIMPLIFIED_CHINESE ? r->name_zh:r->name_en;
    d->description=language==CITY_LANGUAGE_SIMPLIFIED_CHINESE ? r->description_zh:r->description_en;
    snprintf(rows[i].type,sizeof(rows[i].type),r->type2?"%s / %s":"%s",types[r->type1],types[r->type2]);
    d->type_label=rows[i].type; d->element=types[r->type1]; return d;
}
const lv_image_dsc_t *pokemon_content_sprite(uint16_t id)
{
    if (!pokemon_content_active()) return NULL;
    city_pack_asset_t asset;
    if (!city_pack_asset(&store.active,id,2,&asset)) return NULL;
    const uint8_t *p=mapped+asset.offset;
    unsigned width=p[4]|(unsigned)p[5]<<8, height=p[6]|(unsigned)p[7]<<8;
    static lv_image_dsc_t images[2]; static unsigned cursor;
    lv_image_dsc_t *image=&images[cursor++%2];
    lv_image_cache_drop(image);
    lv_image_header_cache_drop(image);
    *image=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565A8,
        .w=width,.h=height,.stride=width*2},.data_size=asset.length-12,.data=p+12};
    return image;
}
bool pokemon_content_cry_size(uint16_t id,uint32_t *samples)
{
    uint8_t header[12];
    if (!samples || !pokemon_content_active() || !city_catalog_asset_read(&audio_view,id,3,0,header,12)) return false;
    *samples=u32(header+8); return true;
}
bool pokemon_content_cry_read(uint16_t id,uint32_t sample,void *out,size_t bytes)
{
    return sample<=32000 && bytes<=CITY_PACK_READ_BYTES &&
        city_catalog_asset_read(&audio_view,id,3,12+sample*2,out,bytes);
}
bool pokemon_content_verify_cache(void)
{
    if (!pokemon_content_active()) return false;
    city_catalog_page_t *page=malloc(sizeof(*page));
    if (!page) return false;
    bool ok=true;
    for (uint32_t i=0;i<(store.active.count+3)/4 && ok;++i) {
        ok=city_catalog_page(&ui_view,i,page) && page->count<=4;
        for (unsigned j=0;j<page->count && ok;++j) {
            city_pack_species_t direct;
            ok=city_pack_at(&store.active,i*4+j,&direct) &&
                !memcmp(&page->rows[j],&direct,sizeof(direct));
        }
    }
    uint8_t block[CITY_PACK_READ_BYTES];
    for (uint32_t i=0;i<store.active.count && ok;++i) {
        city_pack_species_t row;
        ok=city_catalog_at(&ui_view,i,&row);
        for (unsigned kind=2;kind<=3 && ok;++kind) {
            city_pack_asset_t asset;
            ok=city_pack_asset(&store.active,row.species_id,kind,&asset);
            for (uint32_t offset=0;ok && offset<asset.length;offset+=sizeof(block)) {
                size_t n=asset.length-offset; if (n>sizeof(block)) n=sizeof(block);
                ok=city_catalog_asset_read(&ui_view,row.species_id,kind,offset,block,n) &&
                    !memcmp(block,mapped+asset.offset+offset,n);
            }
        }
    }
    free(page); return ok;
}
