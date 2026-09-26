#include "bme280.h"
static i32 get_tp_params(bme280_calib_t *calib_params);
static i32 get_hum_params(bme280_calib_t *calib_params);
static inline void bme280_set_oversampling_wait(bme280_config_t *config_data);
static inline u8 bme280_translate_oversampling(u8 data);

static i2c_lane_t lane;

void bme280_set_lane(i2c_lane_t bus_lane) {
    lane = bus_lane;
}

bme280_final_data bme280_compensate_data(const bme280_calib_t *calib_params, const volatile bme280_raw_data_t *raw_data) {
    i32 adc_temp = (i32)((raw_data->temp_msb << 12) | (raw_data->temp_lsb << 4) | (raw_data->temp_xlsb >> 4));
    i32 adc_press = (i32)((raw_data->press_msb << 12) | (raw_data->press_lsb << 4) | (raw_data->press_xlsb >> 4));
    i32 adc_hum = (i32)((raw_data->hum_msb << 8) | (raw_data->hum_lsb));

    i32 temp = bme280_compensate_t(calib_params, adc_temp);
    i32 press = bme280_compensate_p(calib_params, adc_press);
    i32 hum = bme280_compensate_h(calib_params, adc_hum);

    return (bme280_final_data){
        .temp = temp,
        .press = press,
        .hum = hum,
    };
}

i32 bme280_start_read_raw_data(volatile bme280_raw_data_t *raw_data) {
    if (i2c_start_bulk_read_async(lane, BME280_I2C_ADDR_PRIM, BME280_REG_DATA_START, (volatile u8 *)raw_data, BME280_LEN_P_T_H_DATA) != 0) {
        return I2C_BUS_BUSY;
    }
    return 0;
}

static u8 bme280_forced_mode_wait_ms = 0;
static u8 bme280_forced_mode_ctrl_meas = 0;

i32 bme280_set_config(bme280_config_t settings) {
    u8 config_addresses[] = {
        BME280_REG_CONFIG,
        BME280_REG_CTRL_HUM,
        BME280_REG_CTRL_MEAS,
    };

    bme280_set_oversampling_wait(&settings);

    u8 config_data[] = {
        settings.config,
        settings.ctrl_hum,
        settings.ctrl_meas,
    };

    i2c_address_data_pair_array data_pairs = {
        .addresses = config_addresses,
        .data = config_data,
        .capacity = 3,
    };

    if (i2c_start_bulk_write_alternating_async(lane, BME280_I2C_ADDR_PRIM, &data_pairs)) {
        return I2C_BUS_BUSY;
    }

    return i2c_wait_completion(lane);
}

static inline void bme280_set_oversampling_wait(bme280_config_t *config_data) {
    bme280_forced_mode_ctrl_meas = (config_data->ctrl_meas & 0xFC) | (BME280_POWERMODE_FORCED << BME280_MODE_LSB);

    u8 temp = bme280_translate_oversampling((config_data->ctrl_meas >> BME280_OSR_T_LSB) & BME280_OVERSAMPLING_MASK);
    u8 press = bme280_translate_oversampling((config_data->ctrl_meas >> BME280_OSR_P_LSB) & BME280_OVERSAMPLING_MASK);
    u8 hum = bme280_translate_oversampling((config_data->ctrl_hum >> BME280_OSR_H_LSB) & BME280_OVERSAMPLING_MASK);

    // datasheet provided formula for worst case timing
    u32 wait_us = (1250 + (2300 * temp) +
                   ((press > 0) ? (2300 * press + 575) : 0) +
                   ((hum > 0) ? (2300 * hum + 575) : 0));

    bme280_forced_mode_wait_ms = (u8)(wait_us / 1000) + 1; // + 1 ms for safety
}

static inline u8 bme280_translate_oversampling(u8 data) {
    switch (data) {
    case 0:
        return 0;
    case BME280_OVERSAMPLING_1X:
        return 1;
    case BME280_OVERSAMPLING_2X:
        return 2;
    case BME280_OVERSAMPLING_4X:
        return 4;
    case BME280_OVERSAMPLING_8X:
        return 8;
    default:
        return 16;
    }
}

// Function to set forced mode
// timing for measurements to complete:
//   t_measure_typical = 1 + [2 * T_oversampling] + [2 * P_oversampling + 0.5] + [2 * H_oversampling + 0.5]
//   t_measure_max = 1.25 + [2.3 * T_oversampling] + [2.3 * P_oversampling + 0.575] + [2.3 * H_oversampling + 0.575]
//
//   ODR_max_force (max sampling rate) = 1000 / t_measure
//
// for default values (*_oversampling = 1):
//   t_measure_typical = 8ms
//   t_measure_max = 9.3ms
//   ODR_max = 107Hz
//
//  safe wait = 10ms - should be time enough for the i2c write to complete too (~3 bytes via i2c @~93hz ~= 240 us)
//
// returns minimum wait time in ms (or -1 for bus busy)
//
i32 bme280_set_forced_mode() {
    u8 addr = BME280_REG_CTRL_MEAS;

    i2c_address_data_pair_array data_pairs = {
        .addresses = &addr,
        .data = &bme280_forced_mode_ctrl_meas,
        .capacity = 1,
    };

    if (i2c_start_bulk_write_alternating_async(lane, BME280_I2C_ADDR_PRIM, &data_pairs)) {
        return I2C_BUS_BUSY;
    }

    return bme280_forced_mode_wait_ms;
}

// blocking function to get calib data
i32 bme280_get_calib_params(bme280_calib_t *calib_params) {
    i32 tp_err = get_tp_params(calib_params);
    if (tp_err != 0) {
        return tp_err;
    }
    i32 hum_err = get_hum_params(calib_params);
    if (hum_err != 0) {
        return hum_err;
    }
    return 0;
}

static i32 get_tp_params(bme280_calib_t *calib_params) {
    if (i2c_start_bulk_read_async(lane, BME280_I2C_ADDR_PRIM, BME280_REG_TEMP_PRESS_CALIB_DATA_START, (u8 *)calib_params, BME280_LEN_TEMP_PRESS_CALIB_DATA)) {
        return I2C_BUS_BUSY;
    }
    return i2c_wait_completion(lane);
}

static i32 get_hum_params(bme280_calib_t *calib_params) {
    u8 buf[BME280_LEN_HUMIDITY_CALIB_DATA];

    if (i2c_start_bulk_read_async(lane, BME280_I2C_ADDR_PRIM, BME280_REG_HUMIDITY_CALIB_DATA, buf, BME280_LEN_HUMIDITY_CALIB_DATA)) {
        return I2C_BUS_BUSY;
    }

    i32 err = i2c_wait_completion(lane);
    if (err != 0) {
        return err;
    }

    calib_params->dig_h2 = COMBINE_I16(buf[0], buf[1]);
    calib_params->dig_h3 = buf[2];

    calib_params->dig_h4 = ((i16)(i8)buf[3] * 16) | (buf[4] & 0x0F);
    calib_params->dig_h5 = ((i16)(i8)buf[5] * 16) | (buf[4] >> 4);

    calib_params->dig_h6 = (i8)buf[6];
    return 0;
}

/* 32 bit Compensation formulas from Bosch BME280 Datasheet */

// t_fine carries fine temperature as a global value
static i32 t_fine;

// returns temperature in DegC, resolution is 0.01 DegC.
// Output value of "5123" equals 51.23 DegC
i32 bme280_compensate_t(const bme280_calib_t *calib, const i32 adc_temp) {
    i32 var1, var2, temp;

    var1 = ((((adc_temp >> 3) - ((i32)calib->dig_t1 << 1))) * ((i32)calib->dig_t2)) >> 11;

    var2 = (((((adc_temp >> 4) - ((i32)calib->dig_t1)) * ((adc_temp >> 4) - ((i32)calib->dig_t1))) >> 12) * ((i32)calib->dig_t3)) >> 14;

    t_fine = var1 + var2;
    temp = (t_fine * 5 + 128) >> 8;
    return temp;
}

// returns pressure in Pa as unsigned 32 bit integer.
// Output value of "96386" equals 96386 Pa = 963.86 hPa
u32 bme280_compensate_p(const bme280_calib_t *calib, const i32 adc_p) {
    i32 var1, var2;
    u32 p;

    var1 = (((i32)t_fine) >> 1) - (i32)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((i32)calib->dig_p6);
    var2 += ((var1 * ((i32)calib->dig_p5)) << 1);
    var2 = (var2 >> 2) + (((i32)calib->dig_p4) << 16);
    var1 = (((calib->dig_p3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) + ((((i32)calib->dig_p2) * var1) >> 1)) >> 18;
    var1 = ((((32768 + var1)) * ((i32)calib->dig_p1)) >> 15);

    if (var1 == 0) {
        // avoid exception caused by division by 0
        return 0;
    }

    p = (((u32)(((i32)1048576) - adc_p) - (var2 >> 12))) * 3125;

    if (p < 0x80000000) {
        p = (p << 1) / ((u32)var1);
    } else {
        p = (p / (u32)var1) * 2;
    }

    var1 = (((i32)calib->dig_p9) * ((i32)(((p >> 3) * (p >> 3)) >> 13))) >> 12;
    var2 = (((i32)(p >> 2)) * ((i32)calib->dig_p8)) >> 13;
    p = (u32)((i32)p + ((var1 + var2 + calib->dig_p7) >> 4));

    return p;
}

// returns humidity in %RH as unsigned 32 bit integer in Q22.10 format (22 integer and 10 fractional bits).
// Output value of "47445" represents 47445/1024 = 46.333 %RH
u32 bme280_compensate_h(const bme280_calib_t *calib, const i32 adc_h) {
    i32 v_x1_u32r;

    v_x1_u32r = (t_fine - ((i32)76800));

    v_x1_u32r = (((((adc_h << 14) - (((i32)calib->dig_h4) << 20) - (((i32)calib->dig_h5) * v_x1_u32r)) + ((i32)16384)) >> 15) * (((((((v_x1_u32r * ((i32)calib->dig_h6)) >> 10) * (((v_x1_u32r * ((i32)calib->dig_h3)) >> 11) + ((i32)32768))) >> 10) + ((i32)2097162)) * ((i32)calib->dig_h2) + 1892) >> 14));

    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((i32)calib->dig_h1)) >> 4));

    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);

    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);

    return (u32)(v_x1_u32r >> 12);
}
