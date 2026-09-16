#include "oled.h"

// Display size 128 x 32 (last 32 rows of internal ram are ignored)
// Ram is divided in 8 pages.
//
// Page0 (com0-com7)
// Page 1 (com1-com15) etc.
//
// In more detail:
//
//     | Seg0   | Seg1   | ... | Seg126   | Seg127 |
//     | lsb d0 | lsb d0 | ... |          |        | com 0
//     | ...    | ...    | ... | ...      |        | com 1
// Page0    ...
//     | msb d7 | msb d7 | ... | ...      |        | com 7
//
//        ^ byte comes here

static u16 oled_tx[1 + 512];
static i2c_lane_t lane;

void oled_build_tx_buffer(const u8 *frame_buffer, u32 len) {
    oled_tx[0] = 0x40; /// control byte for data stream

    for (u32 i = 0; i < len; i++) {
        oled_tx[i + 1] = frame_buffer[i];
    }

    oled_tx[len] |= I2C_IC_DATA_CMD_STOP_BITS;
}

void oled_set_lane(i2c_lane_t bus_lane) {
    lane = bus_lane;
}

b32 oled_init(const u8 *commands) {
    if (i2c_start_bulk_write_async(lane, OLED_I2C_ADDRESS, (u8 *)commands, OLED_DEFAULT_INIT_CMD_LIST_LEN) != 0) {
        return I2C_BUS_BUSY;
    }
    return i2c_wait_completion(lane);
}
