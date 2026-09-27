#include <type_alias.h>
#include <hardware/structs/resets.h>
#include <hardware/structs/ticks.h>
#include <hardware/structs/timer.h>

#include "rtt.h"
#include "driver/bme280.h"
#include "driver/oled.h"

#include "../logo/orsuka_industries_logo.h"

#define PIN25 25

#define RESETS_CLEAR (RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS)

// Hardware gated by i2c bus to a minimum of roughly 52+-1us
#define MAIN_PERIOD_MS 1000

#define BTN_CH 18
#define LED_GREEN_CH 19
#define LED_BLUE_CH 20
#define LED_YELLOW_CH 21
#define LED_MASK (1u << LED_GREEN_CH) | (1u << LED_BLUE_CH) | (1u << LED_YELLOW_CH)

#define BTN_CH_PROC_INDEX (BTN_CH / 8)
#define BTN_CH_EDGE_HIGH (((BTN_CH % 8) * 4) + 3)
#define BTN_CH_EDGE_LOW (((BTN_CH % 8) * 4) + 2)
#define BTN_CH_LEVEL_HIGH (((BTN_CH % 8) * 4) + 1)
#define BTN_CH_LEVEL_LOW (((BTN_CH % 8) * 4) + 0)

#define CH_LOGO 0x0
#define CH_TEMP 0x1
#define CH_HUM 0x2
#define CH_PRESS 0x3

#define PIN_MASK(pin) (1u << pin)

typedef enum {
    APP_IDLE,
    APP_START_READ,
    APP_SENSOR_WAKING,
    APP_READING,
    APP_WRITING,
} app_state;

// functions
void timer0_set_alarm_us(u8 alarm_number, u32 time_to_wait_us);
#define timer0_set_alarm_ms(alarm_number, time_to_wait_ms) timer0_set_alarm_us(alarm_number, 1000 * time_to_wait_ms)

// local functions
static inline void delay_ms(u32 ms_to_wait);
static void log_fault(Writer *writer, i2c_lane_t lane);
static void log_result(Writer *writer, bme280_final_data *data);
static void write_result(Writer *writer, bme280_final_data *data, u32 channel_select);
//static void configure_systick(u8 cycles);
static void first_read_delay(i2c_lane_t lane, bme280_calib_t *calib_params);

// statics and globals
// Systick
volatile u32 ms = 0;
volatile u32 next = 500;

// bme280
static volatile bme280_raw_data_t raw_data;
static bme280_final_data last_data;
static b32 bme280_is_configged = FALSE;
static b32 have_raw = FALSE;

// oled
static const u8 oled_init_commands[] = OLED_DEFAULT_INIT_CMD_LIST;
static b32 oled_is_configged = FALSE;

// main app
static volatile app_state main_state = APP_IDLE;
static volatile u32 ch_select = CH_LOGO;
static volatile u32 ch_last = CH_LOGO;
static u32 waking_until = 0;

b32 raw_btn_pressed() {
    u32 in = sio_hw->gpio_in & PIN_MASK(BTN_CH);
    if (in) {
        return FALSE;
    } else {
        return TRUE;
    }
}

#define DEBOUNCE_MS 25
#define DEBOUNCE_MS_HEX ((u32)((i32)0x80000000 >> (31 - (DEBOUNCE_MS + 1))))
#define DEBOUNCE_MS_HEX_MASK (DEBOUNCE_MS_HEX << 1)
// https://www.ganssle.com/item/debouncing-switches-contacts-hardware.htm
b32 debounce_switch() {
    static u32 state = 1;                                             // gets called 1 ms after gpio interrupt
    state = (state << 1) | !raw_btn_pressed() | DEBOUNCE_MS_HEX_MASK; // 26 zeroes
    if (state == DEBOUNCE_MS_HEX) {                                   // return true if 1 followed by 25 zeroes (held low at least 25ms)
        return TRUE;
    }
    return FALSE;
}

/*
// clk_sys must already be configured. Usually done in `crt0`
static void configure_systick(u8 cycles) {
    ticks_hw->ticks[TICK_PROC0].cycles = cycles;
    ticks_hw->ticks[TICK_PROC0].ctrl = TICKS_PROC0_CTRL_ENABLE_BITS;
    while (!(ticks_hw->ticks[TICK_PROC0].ctrl & TICKS_PROC0_CTRL_RUNNING_BITS)) {}

    m33_hw->syst_rvr = SYSTICK_TOP;
    m33_hw->syst_cvr = 0;
    m33_hw->syst_csr = M33_SYST_CSR_TICKINT_BITS | M33_SYST_CSR_ENABLE_BITS;
}
*/

#define BTN_EVENT (1u << 0)
#define TICK_EVENT (1u << 1)
#define ALARM_TICK 0
#define ALARM_TICK_MASK (1u << ALARM_TICK)
#define ALARM_DEBOUNCE 1
#define ALARM_DEBOUNCE_MASK (1u << ALARM_DEBOUNCE)
#define ALARMS_MASK (ALARM_TICK_MASK | ALARM_DEBOUNCE_MASK)

static volatile u8 events = 0;

void TIMER0_IRQ_0_Handler() {
    u32 triggered = timer0_hw->ints & ALARMS_MASK;
    hw_clear_bits(&timer0_hw->intr, triggered);

    if (triggered & ALARM_TICK_MASK) {
        events |= TICK_EVENT;
    }

    if (triggered & ALARM_DEBOUNCE_MASK) {
        static u8 tries = 1;
        if (debounce_switch()) {
            events |= BTN_EVENT;
            tries = 1;
        } else {
            if (tries++ < DEBOUNCE_MS) {
                timer0_set_alarm_ms(ALARM_DEBOUNCE, 1);
            } else {
                tries = 1;
            }
        }
    }
}

void IO_IRQ_BANK0_Handler() {
    u32 triggered = io_bank0_hw->proc0_irq_ctrl.ints[BTN_CH_PROC_INDEX] & (1u << BTN_CH_EDGE_LOW);
    hw_clear_bits(&io_bank0_hw->proc0_irq_ctrl.inte[BTN_CH_PROC_INDEX], triggered);
    timer0_set_alarm_ms(ALARM_DEBOUNCE, 1);
}

void timer0_set_alarm_us(u8 alarm_number, u32 time_to_wait_us) {
    RT_ASSERT(alarm_number < 4, "There are only 4 alarms..");

    // enable interrupt
    hw_set_bits(&timer0_hw->inte, 1u << alarm_number);

    // enable interrupt at processor level
    hw_set_bits(&m33_hw->nvic_iser[0], 1u << alarm_number); // works for timer0 only, need 4 + number for timer1

    // valid for all delay < UINT32_MAX
    u32 target_us = timer0_hw->timerawl + time_to_wait_us;
    timer0_hw->alarm[alarm_number] = target_us;
}

void resets_clear(u32 mask) {
    hw_clear_bits(&resets_hw->reset, mask);
    while ((resets_hw->reset_done & mask) != mask) {}
}

void pad_input_pullup(u32 pin) {
    pads_bank0_hw->io[pin] = (pads_bank0_hw->io[pin] & ~(PADS_BANK0_GPIO0_OD_BITS | PADS_BANK0_GPIO0_PDE_BITS)) |
                             PADS_BANK0_GPIO0_IE_BITS |
                             PADS_BANK0_GPIO0_PUE_BITS;
}

// temp function to wait ~5us (depends on clk_ref that's why its temp only)
void delay_5_us() {
    for (u32 j = 0; j < 20; j++) {
        __asm__ volatile("nop");
    }
}

void main() {
    char writer_buf[RTT_WRITER_MAX_BUFFER_SIZE];
    Writer rtt_writer_instance;
    Writer *rtt_writer = &rtt_writer_instance;
    WRITER_INIT(rtt_writer, writer_buf, rtt_flush);

    char oled_buf[40];
    Writer oled_writer_instance;
    Writer *oled_writer = &oled_writer_instance;
    WRITER_INIT(oled_writer, oled_buf, oled_flush);

    write_all(rtt_writer, "\nRTT " ANSI_GREEN "OK\n" ANSI_CLEAR);
    flush(rtt_writer);

    // Always first clear reset bits for desired functionalities.
    // In this case: iobank, padsbank
    resets_clear(RESETS_CLEAR);

    //io_bank0_hw -> gpio function selection
    io_bank0_hw->io[PIN25].ctrl = GPIO_FUNC_SIO;
    io_bank0_hw->io[LED_GREEN_CH].ctrl = GPIO_FUNC_SIO;
    io_bank0_hw->io[LED_BLUE_CH].ctrl = GPIO_FUNC_SIO;
    io_bank0_hw->io[LED_YELLOW_CH].ctrl = GPIO_FUNC_SIO;

    // Pads bank -> configure pads for led
    hw_clear_bits(&pads_bank0_hw->io[PIN25], PADS_BANK0_GPIO0_ISO_BITS);
    hw_clear_bits(&pads_bank0_hw->io[LED_GREEN_CH], PADS_BANK0_GPIO0_ISO_BITS);
    hw_clear_bits(&pads_bank0_hw->io[LED_BLUE_CH], PADS_BANK0_GPIO0_ISO_BITS);
    hw_clear_bits(&pads_bank0_hw->io[LED_YELLOW_CH], PADS_BANK0_GPIO0_ISO_BITS);

    // output enable SIO reg. Special atomic registers for SIO
    sio_hw->gpio_oe_set = (1u << PIN25) | LED_MASK;

    // clk_sys must be configured before calling this function.
    //configure_systick(SYST_CYCLES);

    i2c_config i2c1_cfg = {
        .sda_pin = GP14,
        .scl_pin = GP15,
    };

    // Init button
    pads_bank0_hw->io[BTN_CH] = (pads_bank0_hw->io[BTN_CH] &
                                    ~(PADS_BANK0_GPIO0_OD_BITS |
                                        PADS_BANK0_GPIO0_PDE_BITS |
                                        PADS_BANK0_GPIO0_PUE_BITS)) |
                                PADS_BANK0_GPIO0_IE_BITS;

    io_bank0_hw->io[BTN_CH].ctrl = GPIO_FUNC_SIO;
    hw_clear_bits(&pads_bank0_hw->io[BTN_CH], (PADS_BANK0_GPIO0_ISO_BITS));
    hw_set_bits(&io_bank0_hw->proc0_irq_ctrl.inte[BTN_CH_PROC_INDEX], 1u << BTN_CH_EDGE_LOW);
    hw_set_bits(&m33_hw->nvic_iser[0], IO_IRQ_BANK0);

    // I2C master enable
    i2c_init_master(&i2c1_cfg);
    i2c_bus_probe(rtt_writer, i2c1_cfg.lane);
    i2c_irq_enable(i2c1_cfg.lane);

    bme280_set_lane(i2c1_cfg.lane);
    bme280_calib_t calib_params;
    i32 calib_error = bme280_get_calib_params(&calib_params);

    if (calib_error != 0) {
        print(rtt_writer, "get_calib_params error: {d}\n", calib_error);
        log_fault(rtt_writer, i2c1_cfg.lane);
        PANIC;
    }

    bme280_config_t config_data = {
        .config = BME280_DEFAULT_CONFIG,
        .ctrl_hum = BME280_DEFAULT_CTRL_HUM,
        .ctrl_meas = BME280_DEFAULT_CTRL_MEAS, // init to sleep mode
    };

    i32 config_error = bme280_set_config(config_data);

    if (config_error != 0) {
        print(rtt_writer, "set_config error: {d}\n", config_error);
        log_fault(rtt_writer, i2c1_cfg.lane);
        PANIC;
    }

    u8 readback[4];
    i2c_start_bulk_read_async(i2c1_cfg.lane, BME280_I2C_ADDR_PRIM, BME280_REG_CTRL_HUM, readback, 4); //0xf2..0xf5
    i32 readback_err = i2c_wait_completion(i2c1_cfg.lane);
    if (readback_err != 0) {
        print(rtt_writer, "set_config error: {u:x}\n", readback_err);
        log_fault(rtt_writer, i2c1_cfg.lane);
        PANIC;
    }

    if (readback[0] == config_data.ctrl_hum && readback[2] == (config_data.ctrl_meas & 0xFC) && readback[3] == config_data.config) {
        write_all(rtt_writer, "\nBME280 SETUP " ANSI_GREEN "OK" ANSI_CLEAR "\n");
        bme280_is_configged = TRUE;
    } else {
        write_all(rtt_writer, "\nBME280 SETUP " ANSI_RED "FAILED" ANSI_CLEAR "\n");
        write_all(rtt_writer, "  Printing config readout:\n");
        print(rtt_writer, "    hum={u:xb}\n    meas={u:xb}\n    cfg={u:xb}\n\n", readback[0], readback[2], readback[3]);
        flush(rtt_writer);
        PANIC;
    }
    flush(rtt_writer);

    // avoid 0 reading due to off-by-one frame lag in architecture
    first_read_delay(i2c1_cfg.lane, &calib_params);

    // Initialise OLED
    b32 oled_err = oled_init(oled_init_commands, i2c1_cfg.lane, 0);
    if (oled_err != 0) {
        print(rtt_writer, "oled_init error: {u:x}\n", oled_err);
        log_fault(rtt_writer, i2c1_cfg.lane);
        PANIC;
    } else {
        write_all(rtt_writer, "OLED SETUP " ANSI_GREEN "OK" ANSI_CLEAR "\n\n\n");
        oled_is_configged = TRUE;
    }

    for (;;) {
        u8 flags = events;
        if (flags & BTN_EVENT) {
            events &= ~BTN_EVENT;
            hw_set_bits(&io_bank0_hw->proc0_irq_ctrl.inte[BTN_CH_PROC_INDEX], 1u << BTN_CH_EDGE_LOW); // re enable button irq
            ch_select = (ch_select + 1) & 0x3;
        }

        if ((flags & TICK_EVENT) && (main_state == APP_IDLE)) {
            events &= ~TICK_EVENT;
            timer0_set_alarm_ms(ALARM_TICK, 1000);
            main_state = APP_START_READ;
        }

        switch (main_state) {
        case APP_START_READ: {
            i32 wait_ms = bme280_set_forced_mode();
            if (wait_ms != I2C_BUS_BUSY) {
                main_state = APP_SENSOR_WAKING;
                waking_until = ms + wait_ms;
            }
            break;
        }

        case APP_SENSOR_WAKING: {
            i2c_state i2c1_state = i2c_poll_state(i2c1_cfg.lane);

            if (i2c1_state == I2C_WRITING) {
                break;
            }

            if (i2c1_state == I2C_ERROR) {
                log_fault(rtt_writer, i2c1_cfg.lane);
            }

            if ((i32)(ms - waking_until) >= 0) {
                if (bme280_start_read_raw_data(&raw_data) == 0) {
                    main_state = APP_READING;
                    u32 ch = ch_last;
                    oled_clear();
                    write_result(oled_writer, &last_data, ch);
                    oled_commit_tx_buffer();
                }
            }

            i2c_release(i2c1_cfg.lane);
            break;
        }

        case APP_READING: {
            i2c_state i2c1_state = i2c_poll_state(i2c1_cfg.lane);

            if (i2c1_state == I2C_READING) { //still reading
                break;
            }

            if (i2c1_state == I2C_DONE) { //no error
                have_raw = TRUE;

            } else if (i2c1_state == I2C_ERROR) {
                log_fault(rtt_writer, i2c1_cfg.lane);
            }

            if (oled_start_dma_write() == 0) {
                if (have_raw) {
                    last_data = bme280_compensate_data(&calib_params, &raw_data);
                    have_raw = FALSE;
                    log_result(rtt_writer, &last_data);
                }
                main_state = APP_WRITING;
            }

            i2c_release(i2c1_cfg.lane);
            break;
        }

        case APP_WRITING: {
            i2c_state i2c1_state = i2c_poll_state(i2c1_cfg.lane);

            if (i2c1_state == I2C_WRITING) { // not done yet
                break;
            }

            if (i2c1_state != I2C_DONE) {
                log_fault(rtt_writer, i2c1_cfg.lane);
            }

            i2c_release(i2c1_cfg.lane);
            main_state = APP_IDLE;
            break;
        }

        case APP_IDLE:
            WFI;
            break;
        }
    }
}

static inline void delay_ms(u32 ms_to_wait) {
    u32 wait_until = ms + ms_to_wait;
    while ((i32)(ms - wait_until) < 0) {
        WFI;
    }
}

static void log_fault(Writer *writer, i2c_lane_t lane) {
    u32 fault = i2c_get_fault(lane);
    u32 abrt = i2c_get_abrt_source(lane);
    print(writer, "i2c error: fault={u:x} abrt={u:x}\n", fault, abrt);

#if I2C_DEBUG
    print(writer, "hits={u} stat={u:x} mask={u:x}\n", dbg.isr_hits, dbg.intr_stat, dbg.intr_mask);
    print(writer, "state={u} rxflr={u} txflr={u}\n", dbg.state, dbg.rxflr, dbg.txflr);
#endif

    flush(writer);
}

static void log_result(Writer *writer, bme280_final_data *data) {
    print(writer, ANSI_RETURN_CARRIAGE "readout: temp: {d}, press: {d}, hum: {d}\n", data->temp, data->press, data->hum);
    flush(writer);
}

static void write_result(Writer *writer, bme280_final_data *data, u32 channel_select) {
    i32 temp_int = data->temp / 100;
    u32 temp_frac = (data->temp < 0) ? (u32)((-1 * data->temp) % 100) : (u32)(data->temp % 100);
    i32 press_int = data->press / 100;
    u32 press_frac = data->press % 100;
    i32 hum_int = data->hum / 1024;
    u32 hum_frac = ((data->hum % 1024) * 1000) / 1024;

    switch (channel_select) {
    case CH_LOGO:
        sio_hw->gpio_set = LED_MASK;
        oled_draw_bitmap(0, 0, 128, 32, orsuka_industries_logo, FALSE);
        break;
    case CH_TEMP:
        sio_hw->gpio_clr = LED_MASK;
        sio_hw->gpio_set = 1u << LED_YELLOW_CH;
        print(writer, "TEMPERATURE:\n{d}.{u>2}C", temp_int, temp_frac);
        break;
    case CH_HUM:
        sio_hw->gpio_clr = LED_MASK;
        sio_hw->gpio_set = 1u << LED_BLUE_CH;
        print(writer, "HUMIDITY:\n{d}.{u>3}rH", hum_int, hum_frac);
        break;
    case CH_PRESS:
        sio_hw->gpio_clr = LED_MASK;
        sio_hw->gpio_set = 1u << LED_GREEN_CH;
        print(writer, "PRESSURE:\n{d}.{u>2}hPa", press_int, press_frac);
        break;
    default:
        break;
    }
    flush(writer);
}

static void first_read_delay(i2c_lane_t lane, bme280_calib_t *calib_params) {
    i32 wait_ms = bme280_set_forced_mode();
    i2c_wait_completion(lane);
    delay_ms(wait_ms);
    bme280_start_read_raw_data(&raw_data);
    i2c_wait_completion(lane);
    last_data = bme280_compensate_data(calib_params, &raw_data);
}
