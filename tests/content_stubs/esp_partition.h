#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_PARTITION_TYPE_ANY 0xff
#define ESP_PARTITION_SUBTYPE_ANY 0xff
typedef struct {
    const void *flash_chip;
    unsigned type, subtype;
    uint32_t address, size;
    char label[17];
    bool encrypted, readonly;
} esp_partition_t;
typedef struct test_iterator *esp_partition_iterator_t;
esp_partition_iterator_t esp_partition_find(unsigned, unsigned, const char *);
const esp_partition_t *esp_partition_get(esp_partition_iterator_t);
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t);
void esp_partition_iterator_release(esp_partition_iterator_t);
int esp_partition_read(const esp_partition_t *, size_t, void *, size_t);
int esp_partition_write(const esp_partition_t *, size_t, const void *, size_t);
int esp_partition_erase_range(const esp_partition_t *, size_t, size_t);
