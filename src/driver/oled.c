#include "oled.h"

static u16 oled_tx[OLED_FRAME_BUFFER_SIZE + 1];
static i2c_lane_t lane;
static u32 dma_channel;

void oled_build_tx_buffer(const u8 *frame_buffer, u32 len) {
    oled_tx[0] = 0x40; /// control byte for data stream

    for (u32 i = 0; i < len; i++) {
        oled_tx[i + 1] = frame_buffer[i];
    }

    oled_tx[len] |= I2C_IC_DATA_CMD_STOP_BITS;
}

//b32 i2c_start_bulk_write_dma(i2c_lane_t lane, u32 target_address, u16 *commands, u32 count, u32 dma_channel)
b32 oled_start_dma_write() {
    if (i2c_start_bulk_write_dma(lane, OLED_I2C_ADDRESS, oled_tx, OLED_FRAME_BUFFER_SIZE + 1, dma_channel) != 0) {
        return I2C_BUS_BUSY;
    }
    return 0;
}

void oled_set_i2c_statics(i2c_lane_t bus_lane, u32 channel) {
    lane = bus_lane;
    dma_channel = channel;
}

b32 oled_init(const u8 *commands) {
    if (i2c_start_bulk_write_async(lane, OLED_I2C_ADDRESS, (u8 *)commands, OLED_DEFAULT_INIT_CMD_LIST_LEN) != 0) {
        return I2C_BUS_BUSY;
    }
    return i2c_wait_completion(lane);
}
