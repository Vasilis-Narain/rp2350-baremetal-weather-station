#include "i2c_state_machine.h"
/*
    * Interrupt notes:
    *  Use NVIC_ISER to set/read enabled state of interrupts.
    *
    *  Use NVIC_ICER to clear/read enabled state of interrupts.
    *
    *  Use NVIC_ISPR to set/read pending state of interrupts
    *
    *  Use NVIC_ICPR to clear/read pending state of interrupts
    *
    *  Use NVIC_IABR to show active state of each interrupt
    *
    *  Use NVIC_IPRn to set/read interrupt priorities
    *
    * 
*/

/* Pico specific interrupt notes:
 *
 * First enable correct irq using NVIC_ISER (from above)
 *
 * Use IC_INTR_MASK register to mask/unmask interrupts (0 masked, 1 unmasked)
 * Use IC_INTR_STATUS register to read active status of masked registers (0 inactive, 1 active)
 * Use IC_RAW_INTR_STAT register to read active status of raw registers (0 inactive, 1 active)
 * Use CLR_INTR reigster to clear ALL interrupts (just need to read it)
 * Use IC_CLEAR_* registers to clear individual * interrupts
 *
 * START_DET -> start/restart occured
 * STOP_DET -> stop occured (when the sensor is done)
 * TX_OVER -> transmit buffer is full and processor tries to load extra byte (used to possibly retry on next state machine iter)
 * TX_EMPTY -> at or below threshold value set in IC_TX_TL register HW CLEAR ONLY
 * RX_FULL -> receive buffer is full (used to possibly retry on next state machine iter) HW CLEAR ONLY
 * RX_OVER -> receive buffer is full and bme280 tries to send an extra byte (used to possibly retry on next state machine iter)
 * RX_UNDER -> processor attempts to read receive buffer when empty
 *
*/
static void pump_tx_write();
static void pump_tx_read();
static void drain_rx();
static void clear_rx();

//NOTE(vasilis): these are the I2C interrupt handlers:
//void __attribute__((weak, alias("_DEFAULT_Handler"))) I2C0_IRQ_Handler();
//void __attribute__((weak, alias("_DEFAULT_Handler"))) I2C1_IRQ_Handler();

// Make sure to `#define SDA_PIN` and `#define SCL_PIN` to be used by pico.
// Defaults are 14 and 15 respectively.
void i2c_init_master() {
    // Setup pads
    hw_clear_bits(&pads_bank0_hw->io[SDA_PIN], PADS_I2C_CLEAR);
    hw_clear_bits(&pads_bank0_hw->io[SCL_PIN], PADS_I2C_CLEAR);
    hw_set_bits(&pads_bank0_hw->io[SDA_PIN], PADS_I2C_SET);
    hw_set_bits(&pads_bank0_hw->io[SCL_PIN], PADS_I2C_SET);

    // Most i2c config requires enable = 0.
    i2c_hw->enable = 0;
    while (i2c_hw->enable_status & I2C_IC_ENABLE_STATUS_IC_EN_BITS) {}

    // Config I2C1 as master
    i2c_hw->con = I2C_INIT_SET;

    // Specify target address
    i2c_hw->tar = BME280_I2C_ADDR_PRIM;

    // The following numbers calculated from specification formulas for 12Mhz clk_sys
    // lcnt and hcnt ar ic_clk settings
    //
    // TODO(vasilis): calculate these at comptime rather than hardcode
    //
    i2c_hw->ss_scl_hcnt = 48;
    i2c_hw->ss_scl_lcnt = 72;
    i2c_hw->fs_spklen = 1;
    i2c_hw->sda_hold = 4;

    i2c_hw->enable = 1;
}

volatile i2c_state i2c1_state = I2C_IDLE;
typedef struct {
    volatile u8 *buf;
    u8 *write_registers;
    b32 write_is_data;
    u32 issued;
    u32 received;
    u32 length;
    u32 abrt_source;
    u32 fault;
    u8 reg_addr;
} i2c_descriptor;

//static i2c_descriptor i2c0_descriptor;
static i2c_descriptor i2c1_descriptor;

u32 i2c_get_fault() {
    return i2c1_descriptor.fault;
}

u32 i2c_get_received() {
    return i2c1_descriptor.received;
}

u32 i2c_get_abrt_source() {
    return i2c1_descriptor.abrt_source;
}

u32 i2c_abrt_get_dropped() {
    return i2c1_descriptor.length - i2c1_descriptor.received;
}

void i2c_irq_enable(i2c_lane bus_lane) {
    if (bus_lane == I2C0) {
        i2c0_hw->intr_mask = (I2C_IC_INTR_MASK_M_STOP_DET_BITS | I2C_IC_INTR_MASK_M_TX_ABRT_BITS |
                              I2C_IC_INTR_MASK_M_RX_OVER_BITS);
        m33_hw->nvic_iser[1] = 1u << 4;
    } else if (bus_lane == I2C1) {
        i2c1_hw->intr_mask = (I2C_IC_INTR_MASK_M_STOP_DET_BITS | I2C_IC_INTR_MASK_M_TX_ABRT_BITS |
                              I2C_IC_INTR_MASK_M_RX_OVER_BITS);
        m33_hw->nvic_iser[1] = 1u << 5;
    }
}

// DEBUG: storm guard snapshot, remove once write is fixed
volatile u32 dbg_isr_hits;
volatile u32 dbg_intr_stat;
volatile u32 dbg_intr_mask;
volatile u32 dbg_rxflr;
volatile u32 dbg_txflr;
volatile u32 dbg_state;

b32 i2c_start_bulk_read_async(u8 reg_addr, volatile u8 *buf, u32 len) {
    if (i2c1_state != I2C_IDLE) {
        return I2C_BUS_BUSY;
    }

    i2c1_descriptor = (i2c_descriptor){
        .buf = buf,
        .issued = 0,
        .received = 0,
        .length = len,
        .reg_addr = reg_addr,
        .abrt_source = 0,
    };

    dbg_isr_hits = 0; // DEBUG
    i2c1_hw->data_cmd = reg_addr;
    i2c1_state = I2C_READING;
    i2c1_hw->intr_mask |= (I2C_IC_INTR_MASK_M_TX_EMPTY_BITS | I2C_IC_INTR_MASK_M_RX_FULL_BITS);

    return 0;
}

b32 i2c_start_bulk_write_async(i2c_address_data_pair_array *input) {
    if (i2c1_state != I2C_IDLE) {
        return I2C_BUS_BUSY;
    }

    i2c1_descriptor = (i2c_descriptor){
        .buf = input->data,
        .write_registers = input->addresses,
        .length = input->capacity,
    };

    dbg_isr_hits = 0; // DEBUG
    i2c1_state = I2C_WRITING;
    i2c1_hw->intr_mask |= (I2C_IC_INTR_MASK_M_TX_EMPTY_BITS);

    return 0;
}

void I2C1_IRQ_Handler() {
    u32 irq_status = i2c1_hw->intr_stat;

    // DEBUG: storm guard
    if (++dbg_isr_hits > 10000) {
        dbg_intr_stat = irq_status;
        dbg_intr_mask = i2c1_hw->intr_mask;
        dbg_rxflr = i2c1_hw->rxflr;
        dbg_txflr = i2c1_hw->txflr;
        dbg_state = i2c1_state;
        i2c1_hw->intr_mask = 0;
        i2c1_state = I2C_ERROR;
        return;
    }

    if (irq_status & I2C_IC_INTR_STAT_R_TX_ABRT_BITS) {
        u32 abrt_source = i2c1_hw->tx_abrt_source;
        (void)i2c1_hw->clr_tx_abrt;
        (void)i2c1_hw->clr_stop_det;
        if (i2c1_state == I2C_READING || i2c1_state == I2C_WRITING) {
            i2c1_descriptor.abrt_source = abrt_source;
            i2c1_hw->intr_mask &= ~(I2C_IC_INTR_MASK_M_TX_EMPTY_BITS | I2C_IC_INTR_MASK_M_RX_FULL_BITS);
            clear_rx();
            i2c1_descriptor.fault |= I2C_FAULT_ABORT;
            i2c1_state = I2C_ERROR;
        }
        return;
    }

    if (irq_status & I2C_IC_INTR_STAT_R_RX_OVER_BITS) {
        (void)i2c1_hw->clr_rx_over;
        if (i2c1_state == I2C_READING) {
            i2c1_descriptor.fault |= I2C_FAULT_OVERRUN;
            i2c1_state = I2C_ERROR;
        }
    }

    if (irq_status & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        if (i2c1_state == I2C_READING) {
            drain_rx();
        } else {
            clear_rx();
        }
    }

    if (irq_status & I2C_IC_INTR_STAT_R_TX_EMPTY_BITS) {
        if (i2c1_state == I2C_READING) {
            pump_tx_read();
        } else if (i2c1_state == I2C_WRITING) {
            pump_tx_write();
        }
    }

    if (irq_status & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
        (void)i2c1_hw->clr_stop_det;
        if (i2c1_state == I2C_READING) {
            drain_rx();
            i2c1_state = (i2c1_descriptor.received == i2c1_descriptor.length) ? I2C_DONE : I2C_ERROR;
        }
        if (i2c1_state == I2C_WRITING) {
            clear_rx();
            i2c1_state = (i2c1_descriptor.issued == i2c1_descriptor.length) ? I2C_DONE : I2C_ERROR;
        }
    }

    if (i2c1_state != I2C_READING && i2c1_state != I2C_WRITING) {
        i2c1_hw->intr_mask &= ~(I2C_IC_INTR_MASK_M_TX_EMPTY_BITS | I2C_IC_INTR_MASK_M_RX_FULL_BITS);
    }
}

static void drain_rx() {
    while ((i2c1_descriptor.received < i2c1_descriptor.issued) && (i2c_hw->status & I2C_IC_STATUS_RFNE_BITS)) { // same as RX_FULL interrupt
        i2c1_descriptor.buf[i2c1_descriptor.received++] = (u8)(i2c_hw->data_cmd & I2C_IC_DATA_CMD_DAT_BITS);
    }
}

static void pump_tx_read() {
    while ((i2c1_descriptor.issued < i2c1_descriptor.length) && (i2c_hw->status & I2C_IC_STATUS_TFNF_BITS)) { // same as TX_EMPTY interrupt
        u32 cmd = I2C_IC_DATA_CMD_CMD_BITS;
        if (i2c1_descriptor.issued == 0) {
            cmd |= I2C_IC_DATA_CMD_RESTART_BITS;
        }
        if (i2c1_descriptor.issued == i2c1_descriptor.length - 1) {
            cmd |= I2C_IC_DATA_CMD_STOP_BITS;
        }
        i2c_hw->data_cmd = cmd;
        i2c1_descriptor.issued++;
    }
    if (i2c1_descriptor.issued == i2c1_descriptor.length) {
        i2c1_hw->intr_mask &= ~I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
    }
}

static void pump_tx_write() {
    while ((i2c1_descriptor.issued < i2c1_descriptor.length) && (i2c_hw->status & I2C_IC_STATUS_TFNF_BITS)) { // same as TX_EMPTY interrupt
        u32 cmd = 0;
        if (!i2c1_descriptor.write_is_data) {
            cmd = i2c1_descriptor.write_registers[i2c1_descriptor.issued];
        } else {
            cmd = i2c1_descriptor.buf[i2c1_descriptor.issued];
            if (i2c1_descriptor.issued == i2c1_descriptor.length - 1) {
                cmd |= I2C_IC_DATA_CMD_STOP_BITS;
            }
            i2c1_descriptor.issued++;
        }
        i2c1_descriptor.write_is_data = !i2c1_descriptor.write_is_data;
        i2c_hw->data_cmd = cmd;
    }
    if (i2c1_descriptor.issued == i2c1_descriptor.length) {
        i2c1_hw->intr_mask &= ~I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
    }
}

static void clear_rx() {
    while (i2c1_hw->rxflr) {
        (void)(i2c_hw->data_cmd & I2C_IC_DATA_CMD_DAT_BITS);
    }
}
