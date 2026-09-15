#pragma once
#include <type_alias.h>
#include <hardware/structs/pads_bank0.h>
#include <hardware/regs/intctrl.h>
#include <hardware/structs/i2c.h>
#include <hardware/structs/m33.h>
#include "addresses.h"

#define I2C_RESTART_READ_MASK (I2C_IC_DATA_CMD_CMD_BITS | I2C_IC_DATA_CMD_RESTART_BITS)
#define I2C_RESTART_READ_STOP_MASK (I2C_IC_DATA_CMD_CMD_BITS | I2C_IC_DATA_CMD_RESTART_BITS | I2C_IC_DATA_CMD_STOP_BITS)

#ifndef SDA_PIN
#define SDA_PIN 14
#endif

#ifndef SCL_PIN
#define SCL_PIN 15
#endif

#ifndef I2C1_ENABLE
#define I2C1_ENABLE
#endif

#ifdef I2C1_ENABLE
#define i2c_hw i2c1_hw
#endif
#ifdef I2C0_ENABLE
#define i2c_hw i2c0_hw
#endif

#if !defined(I2C1_ENABLE)
#if !defined(I2C0_ENABLE)
#error Must define either `I2C0_ENABLE` or `I2C1_ENABLE`
#endif
#endif

#define I2C_INIT_SET (I2C_IC_CON_MASTER_MODE_VALUE_ENABLED |                    \
                      I2C_IC_CON_SPEED_VALUE_STANDARD << I2C_IC_CON_SPEED_LSB | \
                      I2C_IC_CON_IC_RESTART_EN_BITS |                           \
                      I2C_IC_CON_IC_SLAVE_DISABLE_BITS)

#define PADS_I2C_CLEAR (PADS_BANK0_GPIO0_ISO_BITS | PADS_BANK0_GPIO0_PDE_BITS)

#define PADS_I2C_SET (PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_PUE_BITS)

// Fault error codes
#define I2C_FAULT_ABORT (1u << 0)
#define I2C_FAULT_OVERRUN (1u << 1)

// Bus error
#define I2C_BUS_BUSY -1

typedef enum {
    I2C0,
    I2C1,
} i2c_lane;

typedef enum {
    I2C_IDLE,
    I2C_READING,
    I2C_WRITING,
    I2C_DONE,
    I2C_ERROR,
} i2c_state;

typedef struct {
    u8 *addresses;
    u8 *data;
    u32 capacity;
} i2c_address_data_pair_array;

extern volatile i2c_state i2c1_state;

b32 i2c_start_bulk_read_async(u8 reg_addr, volatile u8 *buf, u32 len);
b32 i2c_start_bulk_write_async(i2c_address_data_pair_array *input);
void i2c_irq_enable(i2c_lane bus_lane);
u32 i2c_get_abrt_source();
u32 i2c_get_received();
u32 i2c_get_fault();
u32 i2c_abrt_get_dropped();

void i2c_init_master();

b32 i2c_blocking_bulk_read_command(u8 start_address, u8 *buffer, u32 length);
void i2c_blocking_read_command(u8 address, u8 *byte);
