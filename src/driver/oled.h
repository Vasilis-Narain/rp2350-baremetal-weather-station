#pragma once
#include <type_alias.h>
#include "oled_addresses.h"
#include "i2c_state_machine.h"
#include "../Writer.h"
#include "../fonts.h"

// Rasterizing
void oled_set_pixel(u32 x, u32 y, b32 set);
b32 oled_get_pixel(u32 x, u32 y);
void oled_draw_text(u32 x, u32 y, FONTS font, char *text, u32 len, b32 inverted);
void oled_draw_bitmap(u32 x, u32 y, u32 width, u32 height, const u8 *bitmap, b32 inverted);
void oled_commit_tx_buffer();
void oled_clear();

// Writing
b32 oled_init(const u8 *commands, i2c_lane_t bus_lane, u32 dma_channel);
b32 oled_start_dma_write();

// Writers
typedef struct {
    u32 x;
    u32 y;
    FONTS font;
    b32 inverted;
} oled_write_desc;

// Unless you frequently plan to change writing positions
// its easier to edit this rather than calling `oled_set_writer_desc`
#ifdef MAIN_FONT
#define OLED_DEFAULT_WRITER_DESC     \
    (oled_write_desc) {              \
        .inverted = FALSE,           \
        .font = MAIN_FONT_ENUM_NAME, \
        .x = 0,                      \
        .y = 0,                      \
    }
#endif

void oled_set_writer_desc(u32 x, u32 y, FONTS font, b32 inverted);
void oled_flush(Writer *writer);
