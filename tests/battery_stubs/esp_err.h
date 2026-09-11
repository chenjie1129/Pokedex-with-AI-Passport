#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND -2
#define ESP_ERR_TIMEOUT -3
static inline const char *esp_err_to_name(esp_err_t e) { (void)e; return "error"; }
#define ESP_ERR_INVALID_ARG -4
#define ESP_ERR_INVALID_SIZE -5
#define ESP_ERR_INVALID_STATE -6
#define ESP_ERR_NVS_NOT_FOUND -7
#define ESP_ERR_NO_MEM -8
