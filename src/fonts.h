#pragma once
#include <type_alias.h>

// Both fonts are stored page-major, the same layout the SSD1306 framebuffer uses:
//   pixel(x, y) = (data[glyph * BYTES_PER_GLYPH + (y >> 3) * WIDTH + x] >> (y & 7)) & 1
// LSB of each byte is the topmost row of its 8-row page.

#define IBM_LOCHAR 32
#define IBM_HICHAR 127 // inclusive, 96 glyphs
#define IBM_FONT_WIDTH 8
#define IBM_FONT_HEIGHT 16
#define IBM_FONT_PAGES 2
#define IBM_BYTES_PER_GLYPH (IBM_FONT_PAGES * IBM_FONT_WIDTH) // 16
extern const u8 IBM_VGA_NORMAL_FONT[];

#define LOCHAR 32
#define HICHAR 126 // inclusive, 95 glyphs - no 127 in this font
#define FONT_WIDTH 6
#define FONT_HEIGHT 16 // 2 pages; ink occupies rows 0..12 (descenders)
#define FONT_PAGES 2
#define BYTES_PER_GLYPH (FONT_PAGES * FONT_WIDTH) // 12
#define FONT_ADVANCE_Y 13
extern const u8 TERMINUS_FONT[];

typedef struct {
    const u8 *font;
    u32 font_width;
    u32 font_height;
    u32 bytes_per_glyph;
    u32 advance_y;
    u32 hichar;
} font_descriptor;
extern const font_descriptor TERMINUS_FONT_DESCRIPTOR;
extern const font_descriptor IBM_VGA_NORMAL_FONT_DESCRIPTOR;
