#pragma once
#include <type_alias.h>
#include "addresses.h"
#include "i2c_state_machine.h"

#define COMBINE_U16(lsb, msb) ((u16)((msb << 8) | lsb))
#define COMBINE_I16(lsb, msb) ((i16)((msb << 8) | lsb))

#define BME280_OSR_T_LSB (5)
#define BME280_OSR_P_LSB (2)
#define BME280_OSR_H_LSB (0)
#define BME280_MODE_LSB (0)
#define BME280_T_SB_LSB (5)
#define BME280_FILTER_LSB (2)

// default settings for Humidity Sensing (from datasheet):
// Sensor mode: forced (@ 1 sample/second) or normal
// Oversampling settings: pressure x0, temperature x1, humidity x1
// IIR filter settings: filter off
#define BME280_DEFAULT_CTRL_MEAS ((BME280_OVERSAMPLING_1X << BME280_OSR_T_LSB) | \
                                  (BME280_OVERSAMPLING_1X << BME280_OSR_P_LSB) | \
                                  (BME280_POWERMODE_NORMAL << BME280_MODE_LSB))

#define BME280_DEFAULT_CTRL_HUM (BME280_OVERSAMPLING_1X << BME280_OSR_H_LSB)

#define BME280_DEFAULT_CONFIG ((BME280_STANDBY_TIME_0_5_MS << BME280_T_SB_LSB) | (BME280_FILTER_COEFF_OFF << BME280_FILTER_LSB))

typedef struct {
    // bulk read 1: 26 bytes
    u16 dig_t1;
    i16 dig_t2;
    i16 dig_t3;

    u16 dig_p1;
    i16 dig_p2;
    i16 dig_p3;
    i16 dig_p4;
    i16 dig_p5;
    i16 dig_p6;
    i16 dig_p7;
    i16 dig_p8;
    i16 dig_p9;

    // pad necessary for reading inplace with bulk read (auto-incrementing address)
    u8 _pad1;
    u8 dig_h1;

    // bulk read 2 : 7 bytes
    // pad not required since reading these params needs a temp buffer anyway
    // and are set by name.
    i16 dig_h2;
    i16 dig_h4;
    i16 dig_h5;
    u8 dig_h3;
    i8 dig_h6;
} bme280_calib_t;

typedef struct {
    u8 config;
    u8 ctrl_hum;
    u8 ctrl_meas;
} bme280_config_t;

typedef struct {
    u8 press_msb;
    u8 press_lsb;
    u8 press_xlsb;
    u8 temp_msb;
    u8 temp_lsb;
    u8 temp_xlsb;
    u8 hum_msb;
    u8 hum_lsb;
} bme280_raw_data_t;

typedef struct {
    i32 temp;
    u32 press;
    u32 hum;
} bme280_final_data;

void bme280_set_lane(i2c_lane_t bus_lane);

i32 bme280_start_read_raw_data(volatile bme280_raw_data_t *raw_data);

i32 bme280_get_calib_params(bme280_calib_t *calib_params);

i32 bme280_set_config(bme280_config_t settings);

// Returns struct holding compensated values.
bme280_final_data bme280_compensate_data(const bme280_calib_t *calib_params, const volatile bme280_raw_data_t *raw_data);

// returns temperature in DegC, resolution is 0.01 DegC.
// Output value of "5123" equals 51.23 DegC
i32 bme280_compensate_t(const bme280_calib_t *calib, const i32 adc_temp);

// returns pressure in Pa as unsigned 32 bit integer.
// Output value of "96386" equals 96386 Pa = 963.86 hPa
u32 bme280_compensate_p(const bme280_calib_t *calib, const i32 adc_pressure);

// returns humidity in %RH as unsigned 32 bit integer in Q22.10 format (22 integer and 10 fractional bits).
// Output value of "47445" represents 47445/1024 = 46.333 %RH
u32 bme280_compensate_h(const bme280_calib_t *calib, const i32 adc_humidity);
