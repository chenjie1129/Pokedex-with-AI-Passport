#include "pocket_policy.h"
bool city_battery_low(int soc) { return soc >= 0 && soc <= 10; }
bool city_battery_critical(int soc) { return soc >= 0 && soc <= 5; }
bool city_pocket_press(city_pocket_policy_t *p, uint64_t now)
{
    const bool woke = p->screen_off;
    p->screen_off = false;
    p->consume_gesture = woke;
    p->last_activity_ms = now;
    return woke;
}
void city_pocket_sleep(city_pocket_policy_t *p)
{
    p->screen_off = true;
    p->consume_gesture = true;
}
bool city_pocket_idle(city_pocket_policy_t *p, uint64_t now, bool busy, int soc)
{
    if (busy) p->last_activity_ms = now;
    if (!busy && !p->screen_off && now >= p->last_activity_ms &&
        now - p->last_activity_ms >= (city_battery_low(soc) ? 15000U : 60000U)) {
        city_pocket_sleep(p);
        return true;
    }
    return false;
}
