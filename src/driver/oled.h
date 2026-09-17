#pragma once
#include <type_alias.h>
#include "oled_addresses.h"
#include "i2c_state_machine.h"
#include "../fonts.h"

// Rasterizing
void oled_set_pixel(u32 x, u32 y, b32 set);
b32 oled_get_pixel(u32 x, u32 y);
void oled_draw_text(u32 x, u32 y, u32 size, char *text);
void oled_draw_bitmap(u32 x, u32 y, u32 width, u32 height, const u8 *bitmap);
void oled_commit_tx_buffer();

// Writing
b32 oled_init(const u8 *commands, i2c_lane_t bus_lane, u32 dma_channel);
b32 oled_start_dma_write();
