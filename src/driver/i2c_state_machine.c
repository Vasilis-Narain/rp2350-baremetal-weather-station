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
extern void resets_clear(u32 mask);
extern void pad_input_pullup(u32 pin);
extern void delay_5_us();

static void pump_tx_write_alternating(i2c_bus *bus);
static void pump_tx_write(i2c_bus *bus);
static void pump_tx_read(i2c_bus *bus);
static void drain_rx(i2c_bus *bus);
static void clear_rx(i2c_bus *bus);
static void abort_dma(i2c_bus *bus);
static b32 i2c_bus_recovery(i2c_config *cfg);

static i2c_bus buses[] = {
    [I2C0] = {.hw = i2c0_hw, .last_tar = ~0},
    [I2C1] = {.hw = i2c1_hw, .last_tar = ~0},
};

static inline b32 i2c_is_cfg_valid(i2c_config *cfg) {

    if (((cfg->sda_pin % 4) == 0) && (((cfg->scl_pin - 1) % 4) == 0)) {
        cfg->lane = I2C0;
        return TRUE;
    }

    if ((((cfg->sda_pin - 2) % 4) == 0) && (((cfg->scl_pin - 3) % 4) == 0)) {
        cfg->lane = I2C1;
        return TRUE;
    }
    return FALSE;
}

static inline void i2c_set_target(i2c_bus *bus, u32 address) {
    if (address != bus->last_tar) {
        bus->hw->enable = 0;
        while (bus->hw->enable_status & I2C_IC_ENABLE_STATUS_IC_EN_BITS) {}
        bus->hw->tar = address;
        bus->hw->enable = 1;
        bus->last_tar = address;
    }
}

i32 i2c_wait_completion(i2c_lane_t lane) {
    i2c_bus *bus = &buses[lane];
    i32 err = 0;
    while (bus->state == I2C_READING || bus->state == I2C_WRITING) {
        WFI;
    }
    BARRIER;

    if (bus->state != I2C_DONE) {
        u32 fault = bus->desc.fault;
        err = fault ? (i32)fault : (i32)I2C_FAULT_UNKOWN;
    }
    bus->state = I2C_IDLE;
    return err;
}

// Make sure to `#define SDA_PIN` and `#define SCL_PIN` to be used by pico.
// Defaults are 14 and 15 respectively.
void i2c_init_master(i2c_config *cfg) {

    if (!i2c_is_cfg_valid(cfg)) {
        ERROR("i2c_handle invalid: pins not matching correct bus lane\n");
        PANIC;
    }

    if (!i2c_bus_recovery(cfg)) {
        ERROR("i2c_bus unrecoverable\n");
        PANIC;
    }

    i2c_lane_t lane = cfg->lane;

    switch (lane) {
    case I2C0:
        resets_clear(RESETS_RESET_I2C0_BITS | RESETS_RESET_DMA_BITS);
        break;
    case I2C1:
        resets_clear(RESETS_RESET_I2C1_BITS | RESETS_RESET_DMA_BITS);
        break;
    }

    i32 sda = cfg->sda_pin;
    i32 scl = cfg->scl_pin;
    i2c_bus *bus = &buses[lane];

    // Setup pads
    io_bank0_hw->io[sda].ctrl = GPIO_FUNC_I2C;
    io_bank0_hw->io[scl].ctrl = GPIO_FUNC_I2C;
    hw_clear_bits(&pads_bank0_hw->io[sda], PADS_I2C_CLEAR);
    hw_clear_bits(&pads_bank0_hw->io[scl], PADS_I2C_CLEAR);
    hw_set_bits(&pads_bank0_hw->io[sda], PADS_I2C_SET);
    hw_set_bits(&pads_bank0_hw->io[scl], PADS_I2C_SET);

    // Most i2c config requires enable = 0.
    bus->hw->enable = 0;
    while (bus->hw->enable_status & I2C_IC_ENABLE_STATUS_IC_EN_BITS) {}

    // Config I2C1 as master
    bus->hw->con = I2C_INIT_SET;

    // The following numbers calculated from specification formulas for 12Mhz clk_sys
    // lcnt and hcnt ar ic_clk settings
    //
    // TODO(vasilis): calculate these at comptime rather than hardcode
    //
    bus->hw->ss_scl_hcnt = 48;
    bus->hw->ss_scl_lcnt = 72;
    bus->hw->fs_spklen = 1;
    bus->hw->sda_hold = 4;
}

u32 i2c_get_fault(i2c_lane_t lane) {
    return buses[lane].desc.fault;
}

u32 i2c_get_received(i2c_lane_t lane) {
    return buses[lane].desc.received;
}

u32 i2c_get_abrt_source(i2c_lane_t lane) {
    return buses[lane].desc.abrt_source;
}

u32 i2c_abrt_get_dropped(i2c_lane_t lane) {
    return buses[lane].desc.length - buses[lane].desc.received;
}

void i2c_irq_enable(i2c_lane_t bus_lane) {
    if (bus_lane == I2C0) {
        i2c0_hw->intr_mask = (I2C_IC_INTR_MASK_M_STOP_DET_BITS | I2C_IC_INTR_MASK_M_TX_ABRT_BITS |
                              I2C_IC_INTR_MASK_M_RX_OVER_BITS);
        m33_hw->nvic_iser[ISER_ARRAY_INDEX(I2C0_IRQ)] = ISER_ARRAY_BIT(I2C0_IRQ);

    } else if (bus_lane == I2C1) {
        i2c1_hw->intr_mask = (I2C_IC_INTR_MASK_M_STOP_DET_BITS | I2C_IC_INTR_MASK_M_TX_ABRT_BITS |
                              I2C_IC_INTR_MASK_M_RX_OVER_BITS);
        m33_hw->nvic_iser[ISER_ARRAY_INDEX(I2C1_IRQ)] = ISER_ARRAY_BIT(I2C1_IRQ);
    }
}

i2c_state i2c_poll_state(i2c_lane_t lane) {
    return buses[lane].state;
}

i32 i2c_release(i2c_lane_t lane) {
    i2c_bus *bus = &buses[lane];
    i2c_state state = bus->state;
    if (state == I2C_READING || state == I2C_WRITING) {
        return I2C_BUS_BUSY;
    }
    bus->state = I2C_IDLE;
    return 0;
}

#if I2C_DEBUG
debug_stats dbg;
#endif

b32 i2c_probe(i2c_lane_t lane, u8 target_address) {
    i2c_bus *bus = &buses[lane];
    if (bus->state != I2C_IDLE) {
        return I2C_BUS_BUSY;
    }

    u32 done_bits = I2C_IC_RAW_INTR_STAT_STOP_DET_BITS | I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS;
    i2c_set_target(bus, target_address);
    (void)bus->hw->clr_intr;

    bus->hw->data_cmd = I2C_IC_DATA_CMD_CMD_BITS | I2C_IC_DATA_CMD_STOP_BITS;

    u32 spins = 100000;
    while (!(bus->hw->raw_intr_stat & done_bits) && --spins) {}

    b32 present = spins && !(bus->hw->tx_abrt_source & I2C_IC_TX_ABRT_SOURCE_ABRT_7B_ADDR_NOACK_BITS);

    (void)bus->hw->clr_tx_abrt;
    while (bus->hw->rxflr) {
        (void)bus->hw->data_cmd;
    }
    while (bus->hw->status & I2C_IC_STATUS_ACTIVITY_BITS) {}
    return present;
}

void i2c_bus_probe(Writer *writer, i2c_lane_t lane) {
    write_all(writer, "\nProbing I2C Peripherals:\n");
    for (u8 i = I2C_MIN_TARGET_ADDRESS; i <= I2C_MAX_TARGET_ADDRESS; i++) {
        b32 result = i2c_probe(lane, i);
        if (result) {
            print(writer, "  probed address: {u:xb}\n", i);
            flush(writer);
        }
    }
}

b32 i2c_start_bulk_read_async(i2c_lane_t lane, u32 target_address, u8 reg_addr, volatile u8 *buf, u32 len) {
    i2c_bus *bus = &buses[lane];

    if (bus->state != I2C_IDLE) {
        return I2C_BUS_BUSY;
    }
    i2c_set_target(bus, target_address);

    bus->desc = (i2c_descriptor){
        .buf = buf,
        .issued = 0,
        .received = 0,
        .length = len,
        .reg_addr = reg_addr,
        .abrt_source = 0,
    };

    bus->isr_hits = 0;
    bus->hw->data_cmd = reg_addr;

    bus->state = I2C_READING;
    bus->hw->intr_mask |= (I2C_IC_INTR_MASK_M_TX_EMPTY_BITS | I2C_IC_INTR_MASK_M_RX_FULL_BITS);

    return 0;
}

b32 i2c_start_bulk_write_alternating_async(i2c_lane_t lane, u32 target_address, i2c_address_data_pair_array *input) {
    i2c_bus *bus = &buses[lane];
    if (bus->state != I2C_IDLE) {
        return I2C_BUS_BUSY;
    }

    i2c_set_target(bus, target_address);

    bus->desc = (i2c_descriptor){
        .buf = input->data,
        .write_registers = input->addresses,
        .length = input->capacity,
        .write_type = I2C_ALTERNATING,
    };

    bus->isr_hits = 0;

    bus->state = I2C_WRITING;
    bus->hw->intr_mask |= (I2C_IC_INTR_MASK_M_TX_EMPTY_BITS);

    return 0;
}

b32 i2c_start_bulk_write_async(i2c_lane_t lane, u32 target_address, u8 *commands, u32 len) {
    i2c_bus *bus = &buses[lane];
    if (bus->state != I2C_IDLE) {
        return I2C_BUS_BUSY;
    }

    i2c_set_target(bus, target_address);

    bus->desc = (i2c_descriptor){
        .write_registers = commands,
        .length = len,
        .write_type = I2C_SEQUENTIAL,
    };

    bus->isr_hits = 0;

    bus->state = I2C_WRITING;
    bus->hw->intr_mask |= (I2C_IC_INTR_MASK_M_TX_EMPTY_BITS);

    return 0;
}

b32 i2c_start_bulk_write_dma(i2c_lane_t lane, u32 target_address, u16 *commands, u32 count, u32 dma_channel) {
    i2c_bus *bus = &buses[lane];
    if (bus->state != I2C_IDLE) {
        return I2C_BUS_BUSY;
    }

    i2c_set_target(bus, target_address);
    bus->desc = (i2c_descriptor){
        .write_type = I2C_DMA,
        .dma_channel = dma_channel,
    };
    bus->state = I2C_WRITING;
    u32 data_request = (lane == I2C0) ? DREQ_I2C0_TX : DREQ_I2C1_TX;

    dma_hw->ch[dma_channel].read_addr = (u32)commands;
    dma_hw->ch[dma_channel].write_addr = (u32)&bus->hw->data_cmd;
    dma_hw->ch[dma_channel].transfer_count = count;

    dma_hw->ch[dma_channel].ctrl_trig = ((DMA_CH0_CTRL_TRIG_INCR_READ_BITS) |
                                         (DMA_CH0_CTRL_TRIG_DATA_SIZE_VALUE_SIZE_HALFWORD << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB) |
                                         (data_request << DMA_CH0_CTRL_TRIG_TREQ_SEL_LSB) |
                                         (DMA_CH0_CTRL_TRIG_EN_BITS));

    bus->hw->dma_cr = I2C_IC_DMA_CR_TDMAE_BITS;
    return 0;
}

void i2c_irq(i2c_bus *bus) {
    u32 irq_status = bus->hw->intr_stat;

    if (++bus->isr_hits > 10000) {

        DEBUG(irq_status, bus);

        bus->hw->intr_mask = 0;
        bus->desc.fault |= I2C_FAULT_STORM;
        bus->state = I2C_ERROR;
        return;
    }

    if (irq_status & I2C_IC_INTR_STAT_R_TX_ABRT_BITS) {
        u32 abrt_source = bus->hw->tx_abrt_source;
        if (bus->desc.write_type == I2C_DMA) {
            bus->hw->dma_cr = 0;
            abort_dma(bus);
        }
        (void)bus->hw->clr_tx_abrt;
        (void)bus->hw->clr_stop_det;
        if (bus->state == I2C_READING || bus->state == I2C_WRITING) {

            DEBUG(irq_status, bus);

            bus->desc.abrt_source = abrt_source;
            bus->hw->intr_mask &= ~(I2C_IC_INTR_MASK_M_TX_EMPTY_BITS | I2C_IC_INTR_MASK_M_RX_FULL_BITS);
            clear_rx(bus);
            bus->desc.fault |= I2C_FAULT_ABORT;
            bus->state = I2C_ERROR;
        }
        return;
    }

    if (irq_status & I2C_IC_INTR_STAT_R_RX_OVER_BITS) {
        (void)bus->hw->clr_rx_over;
        if (bus->state == I2C_READING) {

            DEBUG(irq_status, bus);

            bus->desc.fault |= I2C_FAULT_OVERRUN;
            bus->state = I2C_ERROR;
        }
    }

    if (irq_status & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        if (bus->state == I2C_READING) {
            drain_rx(bus);
        } else {
            clear_rx(bus);
        }
    }

    if (irq_status & I2C_IC_INTR_STAT_R_TX_EMPTY_BITS) {
        if (bus->state == I2C_READING) {
            pump_tx_read(bus);
        } else if (bus->state == I2C_WRITING) {
            switch (bus->desc.write_type) {
            case I2C_SEQUENTIAL:
                pump_tx_write(bus);
                break;
            case I2C_ALTERNATING:
                pump_tx_write_alternating(bus);
                break;
            case I2C_DMA:
                break;
            }
        }
    }

    if (irq_status & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
        (void)bus->hw->clr_stop_det;
        if (bus->state == I2C_READING) {
            drain_rx(bus);
            if (bus->desc.received == bus->desc.length) {
                bus->state = I2C_DONE;
            } else {
                bus->desc.fault |= I2C_FAULT_EARLY_STOP;
                bus->state = I2C_ERROR;
            }
        }
        if (bus->state == I2C_WRITING) {
            clear_rx(bus);
            switch (bus->desc.write_type) {
            case I2C_DMA:
                bus->hw->dma_cr = 0;
                if (dma_hw->ch[bus->desc.dma_channel].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) {
                    abort_dma(bus);
                    bus->desc.fault |= I2C_FAULT_EARLY_STOP;
                    bus->state = I2C_ERROR;
                } else {
                    bus->state = I2C_DONE;
                }
                break;
            default:
                if (bus->desc.issued == bus->desc.length) {
                    bus->state = I2C_DONE;
                } else {
                    bus->desc.fault |= I2C_FAULT_EARLY_STOP;
                    bus->state = I2C_ERROR;
                }
                break;
            }
        }
    }

    if (bus->state != I2C_READING && bus->state != I2C_WRITING) {
        bus->hw->intr_mask &= ~(I2C_IC_INTR_MASK_M_TX_EMPTY_BITS | I2C_IC_INTR_MASK_M_RX_FULL_BITS);
    }
}
void I2C0_IRQ_Handler() {
    i2c_irq(&buses[I2C0]);
}
void I2C1_IRQ_Handler() {
    i2c_irq(&buses[I2C1]);
}

static void abort_dma(i2c_bus *bus) {
    dma_hw->abort = 1u << bus->desc.dma_channel;
    while (dma_hw->abort & (1u << bus->desc.dma_channel)) {}
}

static void drain_rx(i2c_bus *bus) {
    while ((bus->desc.received < bus->desc.issued) && (bus->hw->status & I2C_IC_STATUS_RFNE_BITS)) { // same as RX_FULL interrupt
        bus->desc.buf[bus->desc.received++] = (u8)(bus->hw->data_cmd & I2C_IC_DATA_CMD_DAT_BITS);
    }
}

static void pump_tx_read(i2c_bus *bus) {
    while ((bus->desc.issued < bus->desc.length) && (bus->hw->status & I2C_IC_STATUS_TFNF_BITS)) { // same as TX_EMPTY interrupt
        u32 cmd = I2C_IC_DATA_CMD_CMD_BITS;
        if (bus->desc.issued == 0) {
            cmd |= I2C_IC_DATA_CMD_RESTART_BITS;
        }
        if (bus->desc.issued == bus->desc.length - 1) {
            cmd |= I2C_IC_DATA_CMD_STOP_BITS;
        }
        bus->hw->data_cmd = cmd;
        bus->desc.issued++;
    }
    if (bus->desc.issued == bus->desc.length) {
        bus->hw->intr_mask &= ~I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
    }
}

static void pump_tx_write_alternating(i2c_bus *bus) {
    while ((bus->desc.issued < bus->desc.length) && (bus->hw->status & I2C_IC_STATUS_TFNF_BITS)) { // same as TX_EMPTY interrupt
        u32 cmd = 0;
        if (!bus->desc.write_is_data) {
            cmd = bus->desc.write_registers[bus->desc.issued];
        } else {
            cmd = bus->desc.buf[bus->desc.issued];
            if (bus->desc.issued == bus->desc.length - 1) {
                cmd |= I2C_IC_DATA_CMD_STOP_BITS;
            }
            bus->desc.issued++;
        }
        bus->desc.write_is_data = !bus->desc.write_is_data;
        bus->hw->data_cmd = cmd;
    }
    if (bus->desc.issued == bus->desc.length) {
        bus->hw->intr_mask &= ~I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
    }
}

static void pump_tx_write(i2c_bus *bus) {
    while ((bus->desc.issued < bus->desc.length) && (bus->hw->status & I2C_IC_STATUS_TFNF_BITS)) {
        u32 cmd = bus->desc.write_registers[bus->desc.issued];
        if (bus->desc.issued == bus->desc.length - 1) {
            cmd |= I2C_IC_DATA_CMD_STOP_BITS;
        }
        bus->hw->data_cmd = cmd;
        bus->desc.issued++;
    }
    if (bus->desc.issued == bus->desc.length) {
        bus->hw->intr_mask &= ~I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
    }
}

static void clear_rx(i2c_bus *bus) {
    while (bus->hw->rxflr) {
        (void)(bus->hw->data_cmd & I2C_IC_DATA_CMD_DAT_BITS);
    }
}

static b32 i2c_bus_recovery(i2c_config *cfg) { // Recover i2c bus
    u32 sda_pin = cfg->sda_pin;
    u32 scl_pin = cfg->scl_pin;
    u32 sda_mask = 1u << sda_pin;
    u32 scl_mask = 1u << scl_pin;

    pad_input_pullup(sda_pin);
    pad_input_pullup(scl_pin);
    io_bank0_hw->io[sda_pin].ctrl = GPIO_FUNC_SIO;
    io_bank0_hw->io[scl_pin].ctrl = GPIO_FUNC_SIO;
    hw_clear_bits(&pads_bank0_hw->io[sda_pin], PADS_BANK0_GPIO0_ISO_BITS);
    hw_clear_bits(&pads_bank0_hw->io[scl_pin], PADS_BANK0_GPIO0_ISO_BITS);

    sio_hw->gpio_clr = sda_mask | scl_mask;
    sio_hw->gpio_oe_clr = sda_mask | scl_mask;
    delay_5_us();

    u32 status = sio_hw->gpio_in;
    if ((status & sda_mask) && (status & scl_mask)) {
        return TRUE;
    }
    if (!(status & scl_mask)) {
        return FALSE;
    }

    b32 sda_free = (sio_hw->gpio_in & sda_mask) != 0;

    for (u32 i = 0; i < 9; i++) {
        sio_hw->gpio_oe_set = scl_mask; // scl low
        delay_5_us();
        sio_hw->gpio_oe_clr = scl_mask; // release (pull up raises it)
        //
        u32 timeout = 10000;
        while (!(sio_hw->gpio_in & scl_mask)) {
            if (--timeout == 0) {
                return FALSE;
            }
        }
        delay_5_us();

        sda_free = (sio_hw->gpio_in & sda_mask) != 0;
    }

    if (!sda_free) {
        return FALSE;
    }

    // stop condition: sda low while scl low, raise scl then release sda.
    sio_hw->gpio_oe_set = scl_mask; // scl low
    delay_5_us();
    sio_hw->gpio_oe_set = sda_mask; // sda low
    delay_5_us();
    sio_hw->gpio_oe_clr = scl_mask; // scl high
    delay_5_us();
    sio_hw->gpio_oe_clr = sda_mask; // sda raises while scl high
    delay_5_us();

    sio_hw->gpio_oe_clr = sda_mask | scl_mask;
    io_bank0_hw->io[sda_pin].ctrl = GPIO_FUNC_I2C;
    io_bank0_hw->io[scl_pin].ctrl = GPIO_FUNC_I2C;
    return TRUE;
}
