#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool screen_off;
    bool consume_gesture;
    uint64_t last_activity_ms;
} city_pocket_policy_t;
bool city_battery_low(int soc);
bool city_battery_critical(int soc);
bool city_pocket_press(city_pocket_policy_t *policy, uint64_t now_ms);
bool city_pocket_idle(city_pocket_policy_t *policy, uint64_t now_ms, bool busy, int soc);
void city_pocket_sleep(city_pocket_policy_t *policy);
