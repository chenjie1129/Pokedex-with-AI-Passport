#include "user_settings.h"
#include <string.h>

city_settings_t city_settings_defaults(void)
{
    return (city_settings_t){CITY_SETTINGS_DEFAULT_PERCENT, CITY_SETTINGS_DEFAULT_PERCENT, false};
}
bool city_settings_valid(const city_settings_t *s)
{
    return s && s->volume <= 100 && s->brightness >= 10 && s->brightness <= 100;
}
bool city_settings_equal(const city_settings_t *a, const city_settings_t *b)
{
    return a && b && a->volume == b->volume && a->brightness == b->brightness && a->muted == b->muted;
}
city_settings_t city_settings_adjust(city_settings_t s, bool brightness, int delta)
{
    if (!city_settings_valid(&s)) s = city_settings_defaults();
    int value = brightness ? s.brightness : s.volume;
    /* Bound the intent before addition, including extreme injected inputs. */
    if (delta > 100) delta = 100;
    if (delta < -100) delta = -100;
    value += delta;
    const int minimum = brightness ? 10 : 0;
    if (value < minimum) value = minimum;
    if (value > 100) value = 100;
    if (brightness) s.brightness = (uint8_t)value;
    else s.volume = (uint8_t)value;
    return s;
}
city_settings_t city_settings_toggle_mute(city_settings_t s)
{
    s.muted = !s.muted;
    return s;
}
uint8_t city_settings_backlight(const city_settings_t *s, bool off, bool low)
{
    if (off) return 0;
    uint8_t brightness = city_settings_valid(s) ? s->brightness : CITY_SETTINGS_DEFAULT_PERCENT;
    return low && brightness > 30 ? 30 : brightness;
}
static uint32_t checksum(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1));
    }
    return ~crc;
}
bool city_settings_encode(const city_settings_t *s, uint8_t bytes[CITY_SETTINGS_BYTES])
{
    if (!city_settings_valid(s) || !bytes) return false;
    memcpy(bytes, "SET1", 4);
    bytes[4] = 1; bytes[5] = s->volume; bytes[6] = s->brightness; bytes[7] = s->muted;
    const uint32_t crc = checksum(bytes, 8);
    for (unsigned i = 0; i < 4; ++i) bytes[8 + i] = (uint8_t)(crc >> (8 * i));
    return true;
}
bool city_settings_decode(const uint8_t *bytes, size_t length, city_settings_t *s)
{
    if (!bytes || !s || length != CITY_SETTINGS_BYTES || memcmp(bytes, "SET1", 4) ||
        bytes[4] != 1 || bytes[7] > 1) return false;
    uint32_t crc = 0;
    for (unsigned i = 0; i < 4; ++i) crc |= (uint32_t)bytes[8 + i] << (8 * i);
    city_settings_t next = {bytes[5], bytes[6], bytes[7] != 0};
    if (crc != checksum(bytes, 8) || !city_settings_valid(&next)) return false;
    *s = next;
    return true;
}
