#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
// Compile the production adapter against an injected register bus.
#include "../components/bsp/src/bsp_battery.c"

static uint8_t regs[256];
static unsigned profile_writes, removed, delay_ms, writes;
static int fail_reg, corrupt_reg;
static bool absent, force_invalid_soc;
static uint8_t config_history[8];
static unsigned config_count;
esp_err_t bsp_i2c_init(void) { return ESP_OK; }
void *bsp_i2c_bus(void) { return regs; }
esp_err_t i2c_master_bus_add_device(void *bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *dev)
{
    assert(bus == regs && cfg->device_address == 0x63);
    *dev = regs; return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev)
{ assert(dev == regs); ++removed; return ESP_OK; }
void vTaskDelay(unsigned ticks) { delay_ms += ticks; }
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const void *input, size_t len, void *out, size_t count, int timeout)
{
    (void)timeout;
    assert(dev == regs && len == 1);
    if (absent) return ESP_FAIL;
    const uint8_t reg = *(const uint8_t *)input;
    memcpy(out, regs + reg, count);
    if (reg == corrupt_reg) *(uint8_t *)out ^= 1;
    return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const void *input, size_t len, int timeout)
{
    (void)timeout;
    assert(dev == regs && len == 2);
    const uint8_t *data = input;
    if (data[0] == fail_reg) return ESP_FAIL;
    ++writes;
    if (data[0] >= 0x10 && data[0] < 0x60) {
        assert(regs[0x08] == 0xF0); // Profile writes require sleep mode.
        ++profile_writes;
    }
    if (data[0] == 0x0B) assert(profile_writes == 80); // Mark valid only after full write.
    if (data[0] == 0x08) config_history[config_count++] = data[1];
    regs[data[0]] = data[1];
    if (data[0] == 0x08 && data[1] == 0 && (regs[0x0B] & 0x80) && !force_invalid_soc)
        regs[0x04] = 64;
    return ESP_OK;
}
static void reset(void)
{
    s_dev = NULL; memset(regs, 0, sizeof(regs));
    regs[0] = 0x0F; regs[4] = 255;
    profile_writes = removed = delay_ms = writes = config_count = 0;
    fail_reg = corrupt_reg = -1;
    absent = force_invalid_soc = false;
}
int main(void)
{
    reset();
    assert(bsp_battery_init() == ESP_OK);
    assert(profile_writes == 80 && (regs[0x0B] & 0x80));
    assert(memcmp(regs + 0x10, s_battery_profile, 80) == 0);
    assert(config_count == 4 && config_history[0] == 0x30 && config_history[1] == 0xF0 &&
           config_history[2] == 0x30 && config_history[3] == 0);
    assert(delay_ms >= 160 && bsp_battery_soc() == 64);
    // Reopening with a matching stock profile performs no writes.
    s_dev = NULL; writes = 0;
    assert(bsp_battery_init() == ESP_OK && writes == 0);
    regs[4] = 255; assert(bsp_battery_soc() == -1);
    reset(); absent = true;
    assert(bsp_battery_init() == ESP_ERR_NOT_FOUND && !s_dev && removed == 1);
    reset(); fail_reg = 0x1F;
    assert(bsp_battery_init() == ESP_FAIL && !s_dev && !(regs[0x0B] & 0x80));
    reset(); corrupt_reg = 0x1F;
    assert(bsp_battery_init() == ESP_FAIL && !s_dev && !(regs[0x0B] & 0x80));
    reset(); force_invalid_soc = true;
    assert(bsp_battery_init() == ESP_ERR_TIMEOUT && !s_dev && delay_ms >= 5000);
    puts("Battery adapter: initialization, profile readback and failure paths passed");
    return 0;
}
