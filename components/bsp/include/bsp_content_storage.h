#pragma once
#include "content_install.h"
#include "esp_partition.h"
/* Optional FUTURE layout only. Current shipping partitions.csv has no content
 * slots, so open fails without any flash I/O. A reviewed migration is required.
 * This adapter and its IO callbacks belong exclusively to one installer worker. */
#define BSP_CONTENT_SLOT_BYTES 0x1a0000U
#define BSP_CONTENT_SECTOR_BYTES 0x1000U
typedef struct {
    const esp_partition_t *slots[2], *control;
    bool ready;
} bsp_content_storage_t;
bool bsp_content_storage_open(bsp_content_storage_t *, city_content_io_t *out);
