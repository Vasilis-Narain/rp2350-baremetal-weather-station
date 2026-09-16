#pragma once
#include <type_alias.h>
#include <hardware/structs/pads_bank0.h>
#include <hardware/structs/io_bank0.h>
#include <hardware/regs/resets.h>
#include <hardware/regs/intctrl.h>
#include <hardware/structs/i2c.h>
#include <hardware/structs/m33.h>
#include <hardware/structs/dma.h>
#include <hardware/regs/dreq.h>
#include "addresses.h"

#define I2C_RESTART_READ_MASK (I2C_IC_DATA_CMD_CMD_BITS | I2C_IC_DATA_CMD_RESTART_BITS)
#define I2C_RESTART_READ_STOP_MASK (I2C_IC_DATA_CMD_CMD_BITS | I2C_IC_DATA_CMD_RESTART_BITS | I2C_IC_DATA_CMD_STOP_BITS)

#define I2C_MIN_TARGET_ADDRESS 0x08
#define I2C_MAX_TARGET_ADDRESS 0x77

typedef enum {
    I2C0,
    I2C1,
} i2c_lane_t;

typedef enum {
    GP0 = 0,
    GP4 = 4,
    GP8 = 8,
    GP12 = 12,
    GP16 = 16,
    GP20 = 20,
} i2c0_sda_pin;

typedef enum {
    GP1 = 1,
    GP5 = 5,
    GP9 = 9,
    GP13 = 13,
    GP17 = 17,
    GP21 = 21,
} i2c0_scl_pin;

typedef enum {
    GP2 = 2,
    GP6 = 6,
    GP10 = 10,
    GP14 = 14,
    GP18 = 18,
    GP22 = 22,
} i2c1_sda_pin;

typedef enum {
    GP3 = 3,
    GP7 = 7,
    GP11 = 11,
    GP15 = 15,
    GP19 = 19,
    GP23 = 23,
} i2c1_scl_pin;

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

typedef struct {
    volatile u8 *buf;
    u8 *write_registers;
    b32 write_is_data;
    b32 write_is_alternating;
    u32 issued;
    u32 received;
    u32 length;
    u32 abrt_source;
    u32 fault;
    u8 reg_addr;
} i2c_descriptor;

typedef struct {
    i32 sda_pin;
    i32 scl_pin;
    i2c_lane_t lane;
} i2c_config;

typedef struct {
    i2c_hw_t *hw;
    i2c_descriptor desc;
    volatile i2c_state state;
    u32 last_tar;
    u32 isr_hits;
} i2c_bus;

#if I2C_DEBUG
typedef struct {
    volatile u32 isr_hits;
    volatile u32 intr_stat;
    volatile u32 intr_mask;
    volatile u32 rxflr;
    volatile u32 txflr;
    volatile u32 state;
} debug_stats;
extern debug_stats dbg;
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
#define I2C_FAULT_STORM (1u << 2)
#define I2C_FAULT_EARLY_STOP (1u << 3)
#define I2C_FAULT_UNKOWN (1u << 7)

// Bus error
#define I2C_BUS_BUSY -1
#define I2C_RETRY_QUEUE_FULL -(1 << 1)

b32 i2c_probe(i2c_lane_t lane, u8 address);
b32 i2c_start_bulk_read_async(i2c_lane_t lane, u32 target_address, u8 reg_addr, volatile u8 *buf, u32 len);
b32 i2c_start_bulk_write_alternating_async(i2c_lane_t lane, u32 target_address, i2c_address_data_pair_array *input);
void i2c_irq_enable(i2c_lane_t lane);
u32 i2c_get_abrt_source(i2c_lane_t lane);
u32 i2c_get_received(i2c_lane_t lane);
u32 i2c_get_fault(i2c_lane_t lane);
u32 i2c_abrt_get_dropped(i2c_lane_t lane);

i2c_state i2c_poll_state(i2c_lane_t lane);
i32 i2c_release(i2c_lane_t lane);
i32 i2c_wait_completion(i2c_lane_t lane);
void i2c_init_master(i2c_config *cfg);

b32 i2c_blocking_bulk_read_command(u8 start_address, u8 *buffer, u32 length);
void i2c_blocking_read_command(u8 address, u8 *byte);
