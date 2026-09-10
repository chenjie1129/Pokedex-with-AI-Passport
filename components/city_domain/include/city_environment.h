#pragma once

#include <stddef.h>
#include <stdint.h>

#define CITY_WIFI_BSSID_BYTES 6U
#define CITY_ENVIRONMENT_MAX_APS 32U

typedef enum {
    CITY_SCAN_ERROR = 0,
    CITY_SCAN_EMPTY,
    CITY_SCAN_EVIDENCE,
} city_scan_status_t;

/*
 * Raw radio identifiers are transient perception data. They must never be
 * copied into persistence models or evidence logs.
 */
typedef struct {
    uint8_t bssid[CITY_WIFI_BSSID_BYTES];
    int8_t rssi;
} city_wifi_ap_observation_t;

typedef struct {
    city_scan_status_t status;
    const city_wifi_ap_observation_t *aps;
    size_t ap_count;
} city_environment_sample_t;
