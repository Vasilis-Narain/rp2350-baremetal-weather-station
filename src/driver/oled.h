#pragma once
#include <type_alias.h>
#include "oled_addresses.h"
#include "i2c_state_machine.h"
#include "../fonts.h"

// Rasterizing
void oled_build_tx_buffer();
void oled_draw_text(u32 x, u32 y, u32 size, char *text);

// Writing
void oled_set_i2c_statics(i2c_lane_t bus_lane, u32 channel);
b32 oled_init(const u8 *commands);
b32 oled_start_dma_write();
