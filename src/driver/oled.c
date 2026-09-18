#include "oled.h"

// using u16 because 8 bits aren't enough to encode the final stop bit.
// I could use u8 and manually set the stop bit at the end but that would probably
// require a dma irq handler or poll. Trading some space to avoid it
static u16 oled_tx_buffer[OLED_FRAME_BUFFER_SIZE + 1] = {[0] = OLED_CONTROL_BYTE_DATA_STREAM};

// fb pointer starting at data to assist arithmetic
static u16 *const frame_buffer = oled_tx_buffer + 1;

static i2c_lane_t lane;
static u32 dma_channel;

void oled_flush(Writer *writer, va_list args) {
    oled_write_desc desc = (oled_write_desc){
        .x = va_arg(args, u32),
        .y = va_arg(args, u32),
        .size = va_arg(args, u32),
        .inverted = va_arg(args, u32),
    };
    oled_draw_text(desc.x, desc.y, desc.size, writer->buf, writer->current_size, desc.inverted);
    writer->current_size = 0;
}

void oled_commit_tx_buffer() {
    oled_tx_buffer[OLED_FRAME_BUFFER_SIZE] |= I2C_IC_DATA_CMD_STOP_BITS;
}

// Sends `oled_tx_buffer` to i2c device through dma
b32 oled_start_dma_write() {
    if (i2c_start_bulk_write_dma(lane, OLED_I2C_ADDRESS, oled_tx_buffer, OLED_FRAME_BUFFER_SIZE + 1, dma_channel) != 0) {
        return I2C_BUS_BUSY;
    }
    return 0;
}

b32 oled_init(const u8 *commands, i2c_lane_t bus_lane, u32 channel) {
    lane = bus_lane;
    dma_channel = channel;
    if (i2c_start_bulk_write_async(bus_lane, OLED_I2C_ADDRESS, (u8 *)commands, OLED_DEFAULT_INIT_CMD_LIST_LEN) != 0) {
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

static b32 oled_get_pixel_from_bitmap(u32 x, u32 y, u32 width, const u8 *bitmap) {
    return (bitmap[(y >> 3) * width + x] >> (y & 0x7)) & 1u;
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

void oled_draw_bitmap(u32 x, u32 y, u32 width, u32 height, const u8 *bitmap, b32 inverted) {
    for (u32 by = 0; by < height; by++) {
        if (y + by >= OLED_HEIGHT) {
            break;
        }
        for (u32 bx = 0; bx < width; bx++) {
            if (x + bx >= OLED_WIDTH) {
                break;
            }
            b32 value = oled_get_pixel_from_bitmap(bx, by, width, bitmap);
            if (inverted) {
                value = !value;
            }
            oled_set_pixel(x + bx, y + by, value);
        }
    }
}

void oled_draw_text(u32 x, u32 y, u32 size, char *text, u32 len, b32 inverted) {
    const font_descriptor *desc;
    const u8 *character;

    u32 running_x = x;
    u32 running_y = y;

    switch (size) {
    default:
        desc = &TERMINUS_FONT_DESCRIPTOR;
        break;
    case 16:
        desc = &IBM_VGA_NORMAL_FONT_DESCRIPTOR;
        break;
    }

    for (u32 i = 0; i < len; i++) {

        if ((running_x + desc->font_width > OLED_WIDTH) || text[i] == '\n') {
            if ((i + 1 < len) && (text[i] == '\n')) {
                text++;
            }
            running_x = x;
            running_y += desc->advance_y;
        }

        if (running_y >= OLED_HEIGHT) {
            return;
        }

        // Anything outside the font's range falls back to space rather than
        // indexing past the end of the array.
        u32 c = (u8)text[i];
        u32 glyph = (c >= LOCHAR && c <= desc->hichar) ? c - LOCHAR : 0;
        character = desc->font + glyph * desc->bytes_per_glyph;

        oled_draw_bitmap(running_x, running_y, desc->font_width, desc->font_height, character, inverted);

        running_x += desc->font_width;
    }
}
