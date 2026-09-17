#include "oled.h"

static u16 oled_tx_buffer[OLED_FRAME_BUFFER_SIZE + 1] = {[0] = 0x40};

// fb pointer starting at data to assist arithmetic
static u16 *const frame_buffer = oled_tx_buffer + 1;

static i2c_lane_t lane;
static u32 dma_channel;

// To ensure space for a stop bit (bit 9) and to avoid possible bugs
// I opt for using more space and a copy.
void oled_build_tx_buffer() {
    oled_tx_buffer[0] = 0x40; /// control byte for data stream

    for (u32 i = 0; i < OLED_FRAME_BUFFER_SIZE; i++) {
        oled_tx_buffer[i + 1] = frame_buffer[i];
    }

    oled_tx_buffer[OLED_FRAME_BUFFER_SIZE] |= I2C_IC_DATA_CMD_STOP_BITS;
}

// Sends `oled_tx_buffer` to i2c device through dma
b32 oled_start_dma_write() {
    if (i2c_start_bulk_write_dma(lane, OLED_I2C_ADDRESS, oled_tx_buffer, OLED_FRAME_BUFFER_SIZE + 1, dma_channel) != 0) {
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

b32 oled_get_pixel(u32 x, u32 y) {
    RT_ASSERT((x < OLED_WIDTH), "x cannot be greater than or equal to OLED_WIDTH");
    RT_ASSERT((y < OLED_HEIGHT), "y cannot be greater than or equal to OLED_HEIGHT");

    u16 byte_index = (y >> 3) * OLED_WIDTH + x; // y >> 3 for page number (8 pages)
    u16 bit_index = (y & 0x7);
    b32 result = (frame_buffer[byte_index] >> bit_index) & 1u;
    return result;
}

void oled_set_pixel(u32 x, u32 y, b32 set) {
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) {
        return;
    }

    u16 byte_index = (y >> 3) * OLED_WIDTH + x;
    u16 bit_index = (y & 0x7);
    if (set) {
        frame_buffer[byte_index] |= (1u << bit_index);
    } else {
        frame_buffer[byte_index] &= ~(1u << bit_index);
    }
}

void oled_draw_text(u32 x, u32 y, u32 size, char *text);
void oled_draw_bitmap(u32 x, u32 y, u32 width, u32 height, const u8 *bitmap);
