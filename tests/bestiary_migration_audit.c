/* Read-only local save audit. Never print creature details or write device data. */
#include "bestiary_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool read_save(const char *path, city_bestiary_t *out)
{
    FILE *file = fopen(path, "rb"); if (!file) return false;
    uint8_t raw[CITY_BESTIARY_ENCODED_BYTES + 1]; size_t n = fread(raw, 1, sizeof(raw), file);
    bool ok = !ferror(file) && feof(file) && city_bestiary_decode(raw, n, out);
    fclose(file); return ok;
}
int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) return 2;
    city_bestiary_t before, after;
    uint8_t expected[CITY_BESTIARY_ENCODED_BYTES], actual[CITY_BESTIARY_ENCODED_BYTES], sparse[CITY_BESTIARY_ENCODED_BYTES];
    size_t length = 0;
    if (!read_save(argv[1], &before) || !city_bestiary_encode(&before, expected) ||
        !city_bestiary_encode_sparse(&before, sparse, sizeof(sparse), &length) ||
        !city_bestiary_decode(sparse, length, &after) || !city_bestiary_encode(&after, actual) ||
        memcmp(actual, expected, sizeof(actual))) return 1;
    if (argc == 3 && (!read_save(argv[2], &after) || !city_bestiary_encode(&after, actual) ||
                      memcmp(actual, expected, sizeof(actual)))) return 1;
    printf("Canonical state identical; owned=%u buddy=%u sparse_bytes=%zu\n", before.owned_count, before.buddy_instance_id, length);
    return 0;
}
