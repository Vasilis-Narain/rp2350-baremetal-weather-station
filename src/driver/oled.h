#pragma once
#include <type_alias.h>
#include "oled_addresses.h"
#include "i2c_state_machine.h"

// Build tx buffer from frame data
void oled_build_tx_buffer(const u8 *frame_buffer, u32 len);

void oled_set_lane(i2c_lane_t bus_lane);
b32 oled_init(const u8 *commands);
