#include <type_alias.h>
#include <hardware/structs/resets.h>
#include <hardware/structs/ticks.h>

#include "rtt.h"
#include "driver/bme280.h"
#include "driver/oled.h"

#include "../logo/orsuka_industries_logo.h"

#define SYST_CYCLES 12
#define PIN25 25

#define RESETS_CLEAR (RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS)

#define SYSTICK_FREQ_HZ 1000
#define EXT_CLK_FREQ_HZ 1000000
#define SYSTICK_TOP (EXT_CLK_FREQ_HZ / SYSTICK_FREQ_HZ - 1)

// Hardware gated by i2c bus to a minimum of roughly 52+-1ms
#define MAIN_PERIOD_MS 1000

#define BTN_CH 18
#define LED_GREEN_CH 19
#define LED_RED_CH 20
#define LED_YELLOW_CH 21
#define LED_MASK (1u << LED_GREEN_CH) | (1u << LED_RED_CH) | (1u << LED_YELLOW_CH)

#define BTN_CH_LEVEL_HIGH 9
#define BTN_CH_LEVEL_LOW 8
#define BTN_CH_EDGE_HIGH 11
#define BTN_CH_EDGE_LOW 10

#define CH_LOGO 0x0
#define CH_TEMP 0x1
#define CH_HUM 0x2
#define CH_PRESS 0x3

#define BTN_LOCKOUT_MS 40
#define BTN_CONFIRM_MS 30
#define BTN_EDGE_MASK ((1u << BTN_CH_EDGE_LOW) | (1u << BTN_CH_EDGE_HIGH))

typedef enum {
    APP_IDLE,
    APP_START_READ,
    APP_READING,
    APP_WRITING,
} app_state;

// local functions
static inline void delay_ms(u32 ms_to_wait);
static void log_fault(Writer *writer, i2c_lane_t lane);
static void log_result(Writer *writer, bme280_final_data *data);
static void write_result(Writer *writer, bme280_final_data *data, u32 channel_select);
static void configure_systick(u8 cycles);

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
static volatile u32 last_edge_ms = 0;
static volatile b32 btn_pending = FALSE;
static volatile u32 btn_deadline = 0;

void IO_IRQ_BANK0_Handler() {
    u32 latched = io_bank0_hw->proc0_irq_ctrl.ints[2] & BTN_EDGE_MASK;

    io_bank0_hw->intr[2] = latched;
    (void)io_bank0_hw->intr[2];

    u32 now = ms;
    u32 quiet = (u32)(now - last_edge_ms);
    last_edge_ms = now;

    if ((latched & (1u << BTN_CH_EDGE_LOW)) && !btn_pending && quiet >= BTN_LOCKOUT_MS) {
        btn_pending = TRUE;
        btn_deadline = now + BTN_CONFIRM_MS;
    }
}

/* clk_sys must already be configured. Usually done in `crt0`*/
static void configure_systick(u8 cycles) {
    ticks_hw->ticks[TICK_PROC0].cycles = cycles;
    ticks_hw->ticks[TICK_PROC0].ctrl = TICKS_PROC0_CTRL_ENABLE_BITS;
    while (!(ticks_hw->ticks[TICK_PROC0].ctrl & TICKS_PROC0_CTRL_RUNNING_BITS)) {}

    m33_hw->syst_rvr = SYSTICK_TOP;
    m33_hw->syst_cvr = 0;
    m33_hw->syst_csr = M33_SYST_CSR_TICKINT_BITS | M33_SYST_CSR_ENABLE_BITS;
}

void SYSTICK_Handler() {
    ms++;
    if (btn_pending && (i32)(ms - btn_deadline) >= 0) {
        btn_pending = FALSE;
        if (((sio_hw->gpio_in >> BTN_CH) & 1u) == 0) {
            ch_select = (ch_select + 1) & 0x3;
        }
    }
    if (oled_is_configged && ch_last != ch_select && main_state == APP_IDLE) {
        main_state = APP_START_READ;
        ch_last = ch_select;
        return;
    }
    if ((i32)(ms - next) >= 0) {
        next += MAIN_PERIOD_MS;
#if MAIN_DEBUG
        sio_hw->gpio_togl = 1 << PIN25;
#endif
        if (oled_is_configged && main_state == APP_IDLE) {
            main_state = APP_START_READ;
        }
    }
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
    io_bank0_hw->io[LED_RED_CH].ctrl = GPIO_FUNC_SIO;
    io_bank0_hw->io[LED_YELLOW_CH].ctrl = GPIO_FUNC_SIO;

    // Pads bank -> configure pads for led
    hw_clear_bits(&pads_bank0_hw->io[PIN25], PADS_BANK0_GPIO0_ISO_BITS);
    hw_clear_bits(&pads_bank0_hw->io[LED_GREEN_CH], PADS_BANK0_GPIO0_ISO_BITS);
    hw_clear_bits(&pads_bank0_hw->io[LED_RED_CH], PADS_BANK0_GPIO0_ISO_BITS);
    hw_clear_bits(&pads_bank0_hw->io[LED_YELLOW_CH], PADS_BANK0_GPIO0_ISO_BITS);

    // output enable SIO reg. Special atomic registers for SIO
    sio_hw->gpio_oe_set = (1u << PIN25) | LED_MASK;

    // clk_sys must be configured before calling this function.
    configure_systick(SYST_CYCLES);

    i2c_config i2c1_cfg = {
        .sda_pin = GP14,
        .scl_pin = GP15,
    };

    // Enable btn irq
    //pad_input_pullup(BTN_CH);
    pads_bank0_hw->io[BTN_CH] = (pads_bank0_hw->io[BTN_CH] &
                                    ~(PADS_BANK0_GPIO0_OD_BITS |
                                        PADS_BANK0_GPIO0_PDE_BITS |
                                        PADS_BANK0_GPIO0_PUE_BITS)) |
                                PADS_BANK0_GPIO0_IE_BITS;

    io_bank0_hw->io[BTN_CH].ctrl = GPIO_FUNC_SIO;
    hw_clear_bits(&pads_bank0_hw->io[BTN_CH], (PADS_BANK0_GPIO0_ISO_BITS));
    io_bank0_hw->proc0_irq_ctrl.inte[2] |= BTN_EDGE_MASK;

    m33_hw->nvic_iser[ISER_ARRAY_INDEX(IO_IRQ_BANK0)] |= ISER_ARRAY_BIT(IO_IRQ_BANK0);

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
        .ctrl_meas = BME280_DEFAULT_CTRL_MEAS,
    };

    i32 config_error = bme280_set_config(config_data);

    if (config_error != 0) {
        print(rtt_writer, "set_config error: {d}\n", config_error);
        log_fault(rtt_writer, i2c1_cfg.lane);
        PANIC;
    }

    // Delay for first conversion after setting normal mode.
    delay_ms(10);

    u8 readback[4];
    i2c_start_bulk_read_async(i2c1_cfg.lane, BME280_I2C_ADDR_PRIM, BME280_REG_CTRL_HUM, readback, 4); //0xf2..0xf5
    i32 readback_err = i2c_wait_completion(i2c1_cfg.lane);
    if (readback_err != 0) {
        print(rtt_writer, "set_config error: {u:x}\n", readback_err);
        log_fault(rtt_writer, i2c1_cfg.lane);
        PANIC;
    }

    if (readback[0] == config_data.ctrl_hum && readback[2] == config_data.ctrl_meas && readback[3] == config_data.config) {
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
        switch (main_state) {
        case APP_START_READ: {
            if (bme280_start_read_raw_data(&raw_data) == 0) {
                main_state = APP_READING;
                u32 ch = ch_last;
                oled_clear();
                write_result(oled_writer, &last_data, ch);
                oled_commit_tx_buffer();
            }
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
        //print(writer, "{d}.{u>2}C {d}.{u>3}rH\n{d}.{u>2}hPa", temp_int, temp_frac, hum_int, hum_frac, press_int, press_frac);
        oled_draw_bitmap(0, 0, 128, 32, orsuka_industries_logo, FALSE);
        break;
    case CH_TEMP:
        sio_hw->gpio_clr = LED_MASK;
        sio_hw->gpio_set = 1u << LED_YELLOW_CH;
        print(writer, "TEMPERATURE:\n{d}.{u>2}C", temp_int, temp_frac);
        break;
    case CH_HUM:
        sio_hw->gpio_clr = LED_MASK;
        sio_hw->gpio_set = 1u << LED_RED_CH;
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
