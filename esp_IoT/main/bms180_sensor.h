#ifndef BMS180_SENSOR_H
#define BMS180_SENSOR_H

#include <stdint.h>
#include <esp_err.h>
#include <bmp180.h>
#include <i2cdev.h>

typedef struct {
    float temperature_c;
    uint32_t pressure_pa;
    float altitude_m;
} bmp180_data_t;

void BMS180_test_task_start(void);
esp_err_t bmp180_init_sensor(void);
esp_err_t bmp180_read(bmp180_data_t *data);

#endif // BMS180_SENSOR_H