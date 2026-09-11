#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
typedef void *i2c_master_dev_handle_t;
typedef struct { int dev_addr_length; int device_address; int scl_speed_hz; } i2c_device_config_t;
#define I2C_ADDR_BIT_LEN_7 7
esp_err_t bsp_i2c_init(void);
void *bsp_i2c_bus(void);
esp_err_t i2c_master_bus_add_device(void *, const i2c_device_config_t *, i2c_master_dev_handle_t *);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const void *, size_t, void *, size_t, int);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const void *, size_t, int);
