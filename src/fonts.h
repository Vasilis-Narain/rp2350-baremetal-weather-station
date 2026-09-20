#pragma once
#include <type_alias.h>

// Fonts are stored page-major, the same layout the SSD1306 framebuffer uses:
//   pixel(x, y) = (data[glyph * BYTES_PER_GLYPH + (y >> 3) * WIDTH + x] >> (y & 7)) & 1
// LSB of each byte is the topmost row of its 8-row page.

typedef enum {
    FONT_IBM,
    FONT_JMK,
    FONT_TERMINUS,
} FONTS;

typedef struct {
    const u8 *font;
    u32 font_width;
    u32 font_height;
    u32 bytes_per_glyph;
    u32 pages_per_glyph;
    u32 advance_y;
    u32 hichar;
    u32 lochar;
} font_descriptor;

// >>> generated: IBM BEGIN
#define IBM_LOCHAR 32
#define IBM_HICHAR 127 // inclusive, 96 glyphs
#define IBM_FONT_WIDTH 8
#define IBM_FONT_HEIGHT 16
#define IBM_FONT_PAGES 2
#define IBM_BYTES_PER_GLYPH (IBM_FONT_PAGES * IBM_FONT_WIDTH) // 16
extern const u8 IBM_VGA_NORMAL_FONT[];
extern const font_descriptor IBM_VGA_NORMAL_FONT_DESCRIPTOR;
// >>> generated: IBM END

// >>> generated: JMK BEGIN
#define JMK_LOCHAR 32
#define JMK_HICHAR 127 // inclusive, 96 glyphs
#define JMK_FONT_WIDTH 8
#define JMK_FONT_HEIGHT 16
#define JMK_FONT_PAGES 2
#define JMK_BYTES_PER_GLYPH (JMK_FONT_PAGES * JMK_FONT_WIDTH) // 16
#define JMK_FONT_ADVANCE_Y 16
extern const u8 JMK[];
extern const font_descriptor JMK_DESCRIPTOR;
// <<< generated: JMK END

// >>> generated: TERMINUS BEGIN
#define TERMINUS_LOCHAR 32
#define TERMINUS_HICHAR 126 // inclusive, 95 glyphs
#define TERMINUS_FONT_WIDTH 8
#define TERMINUS_FONT_HEIGHT 16
#define TERMINUS_FONT_PAGES 2
#define TERMINUS_BYTES_PER_GLYPH (TERMINUS_FONT_PAGES * TERMINUS_FONT_WIDTH) // 16
#define TERMINUS_FONT_ADVANCE_Y 16
extern const u8 TERMINUS[];
extern const font_descriptor TERMINUS_DESCRIPTOR;
// <<< generated: TERMINUS END
