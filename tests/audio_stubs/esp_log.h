#pragma once
/* Keep arguments type-checked and evaluated without generating log noise. */
#define ESP_LOGI(tag, ...) do { (void)(tag); if (0) printf(__VA_ARGS__); } while (0)
