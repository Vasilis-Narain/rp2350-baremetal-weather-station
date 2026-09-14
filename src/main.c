#include <type_alias.h>
#include <hardware/address_mapped.h>
#include <hardware/structs/resets.h>
#include <hardware/structs/io_bank0.h>
#include <hardware/structs/pads_bank0.h>
#include <hardware/structs/sio.h>
#include <hardware/structs/i2c.h>
#include <hardware/structs/systick.h>
#include <hardware/structs/ticks.h>
#include <hardware/structs/m33.h>

#include "rtt.h"
#include "driver/i2c_state_machine.h"
#include "driver/bme280.h"

#define SYST_CYCLES 12
#define PIN25 25
#define SDA_PIN 14
#define SCL_PIN 15

#define RESETS_CLEAR (RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS | RESETS_RESET_I2C1_BITS)

#define SYSTICK_FREQ_HZ 1000
#define EXT_CLK_FREQ_HZ 1000000
#define SYSTICK_TOP (EXT_CLK_FREQ_HZ / SYSTICK_FREQ_HZ - 1)

// DEBUG: storm guard snapshot from i2c_state_machine.c
extern volatile u32 dbg_isr_hits, dbg_intr_stat, dbg_intr_mask, dbg_rxflr, dbg_txflr, dbg_state;

/* clk_sys must already be configured. Usually done in `crt0`*/
void configure_systick(u8 cycles) {
    ticks_hw->ticks[TICK_PROC0].cycles = cycles;
    ticks_hw->ticks[TICK_PROC0].ctrl = TICKS_PROC0_CTRL_ENABLE_BITS;
    while (!(ticks_hw->ticks[TICK_PROC0].ctrl & TICKS_PROC0_CTRL_RUNNING_BITS)) {}

    // SysTick Control and Status Register
    // 0x00010000 [16]    COUNTFLAG    (0) Returns 1 if timer counted to 0 since last time this was read
    // 0x00000004 [2]     CLKSOURCE    (0) SysTick clock source
    // 0x00000002 [1]     TICKINT      (0) Enables SysTick exception request: +
    // 0x00000001 [0]     ENABLE       (0) Enable SysTick counter: +
    m33_hw->syst_rvr = SYSTICK_TOP;
    m33_hw->syst_cvr = 0;
    m33_hw->syst_csr = M33_SYST_CSR_TICKINT_BITS | M33_SYST_CSR_ENABLE_BITS;
}

static inline void delay(u32 ms_to_wait);

static volatile bme280_raw_data_t raw_data;
static b32 bme280_is_configged = FALSE;
volatile u32 ms = 0;
volatile u32 next = 500;

void SYSTICK_Handler() {
    ms++;
    if ((i32)(ms - next) >= 0) {
        next += 500;
        sio_hw->gpio_togl = 1 << PIN25;
        if (bme280_is_configged) {
            bme280_start_read_raw_data(&raw_data);
        }
    }
}

void resets_clear(u32 mask) {
    hw_clear_bits(&resets_hw->reset, mask);
    while ((resets_hw->reset_done & mask) != mask) {}
}
#define ANSI_RETURN_CARRIAGE "\x1b[1a\r"
#define ANSI_RED "\x1b[31m"
#define ANSI_GREEN "\x1b[32m"
#define ANSI_CLEAR "\x1b[0m"

void main() {
    char writer_buf[RTT_WRITER_MAX_BUFFER_SIZE];
    Writer rtt_writer_instance;
    Writer *rtt_writer = &rtt_writer_instance;

    WRITER_INIT(rtt_writer, writer_buf, rtt_flush);

    // Always first clear reset bits for desired functionalities.
    // In this case: iobank, padsbank, i2c
    resets_clear(RESETS_CLEAR);

    //io_bank0_hw -> gpio function selection
    io_bank0_hw->io[PIN25].ctrl = GPIO_FUNC_SIO;
    io_bank0_hw->io[SDA_PIN].ctrl = GPIO_FUNC_I2C;
    io_bank0_hw->io[SCL_PIN].ctrl = GPIO_FUNC_I2C;

    // Pads bank -> configure pads for led
    hw_clear_bits(&pads_bank0_hw->io[PIN25], PADS_BANK0_GPIO0_ISO_BITS);

    // output enable SIO reg. Special atomic registers for SIO
    sio_hw->gpio_oe_set = 1 << PIN25;

    // clk_sys must be configured before calling this function.
    configure_systick(SYST_CYCLES);

    print(rtt_writer, "\nRTT   {s}OK{s}\n", LITERAL(ANSI_GREEN), LITERAL(ANSI_CLEAR));
    i2c_init_master();
    i2c_irq_enable(I2C1);

    bme280_calib_t calib_params;
    i32 calib_error = bme280_get_calib_params(&calib_params);

    if (calib_error != 0) {
        print(rtt_writer, "get_calib_params error: {d}\n", calib_error);
        flush(rtt_writer);
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
        flush(rtt_writer);
        PANIC;
    }

    // Delay for first conversion after setting normal mode.
    delay(20);
    bme280_is_configged = TRUE;

    u8 readback[4];
    i2c_start_bulk_read_async(BME280_REG_CTRL_HUM, readback, 4); //0xf2..0xf5
    while (i2c1_state == I2C_READING) {
        WFI;
    }
    BARRIER;
    i2c1_state = I2C_IDLE;

    if (readback[0] == config_data.ctrl_hum && readback[2] == config_data.ctrl_meas && readback[3] == config_data.config) {
        print(rtt_writer, "SETUP {s}OK{s}\n\n\n", LITERAL(ANSI_GREEN), LITERAL(ANSI_CLEAR));
    } else {
        print(rtt_writer, "SETUP {s}FAILED{s}\n", LITERAL(ANSI_RED), LITERAL(ANSI_CLEAR));
        write_all(rtt_writer, "  Printing config readout:\n");
        print(rtt_writer, "    hum={u:xb}\n    meas={u:xb}\n    cfg={u:xb}\n\n", readback[0], readback[2], readback[3]);
        flush(rtt_writer);
        PANIC;
    }

    // dont forget to flush :D
    flush(rtt_writer);

    for (;;) {
        if (i2c1_state == I2C_DONE) {
            bme280_final_data data = bme280_compensate_data(&calib_params, &raw_data);
            i2c1_state = I2C_IDLE;
            print(rtt_writer, "\x1B[1A\rreadout: temp: {d}, press: {d}, hum: {d}\n", data.temp, data.press, data.hum);

        } else if (i2c1_state == I2C_ERROR) {
            print(rtt_writer, "write err fault={u:x} abrt={u:x}\n", i2c_get_fault(), i2c_get_abrt_source());
            print(rtt_writer, "hits={u} stat={u:x} mask={u:x}\n", dbg_isr_hits, dbg_intr_stat, dbg_intr_mask);
            print(rtt_writer, "state={u} rxflr={u} txflr={u}\n", dbg_state, dbg_rxflr, dbg_txflr);
            flush(rtt_writer);
            i2c1_state = I2C_IDLE;
            PANIC;
        }
        flush(rtt_writer);
        WFI;
    }
}

static inline void delay(u32 ms_to_wait) {
    u32 wait_until = ms + ms_to_wait;
    while ((i32)(ms - wait_until) < 0) {
        WFI;
    }
}
