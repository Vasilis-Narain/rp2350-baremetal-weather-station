/* From (with minor changes) http://robotcantalk.blogspot.com/2015/03/interfacing-arduino-with-ssd1306-driven.html*/
#pragma once
#include <type_alias.h>

// SLA (0x3C) + WRITE_MODE (0x00) =  0x78 (0b01111000)
#define OLED_I2C_ADDRESS 0x3C

// Control byte
#define OLED_CONTROL_BYTE_CMD_SINGLE 0x80
#define OLED_CONTROL_BYTE_CMD_STREAM 0x00
#define OLED_CONTROL_BYTE_DATA_SINGLE 0xC0
#define OLED_CONTROL_BYTE_DATA_STREAM 0x40

// Fundamental commands (pg.28)
#define OLED_CMD_SET_CONTRAST 0x81 // follow with (reset value 0x7F)
#define OLED_CMD_DISPLAY_RAM 0xA4
#define OLED_CMD_DISPLAY_ALLON 0xA5
#define OLED_CMD_DISPLAY_NORMAL 0xA6
#define OLED_CMD_DISPLAY_INVERTED 0xA7
#define OLED_CMD_DISPLAY_OFF 0xAE
#define OLED_CMD_DISPLAY_ON 0xAF

// Addressing Command Table (pg.30)
#define OLED_CMD_SET_MEMORY_ADDR_MODE 0x20 // follow with 0x00 = HORZ mode = Behave like a KS108 graphic LCD
#define OLED_CMD_SET_COLUMN_RANGE 0x21     // can be used only in HORZ/VERT mode - follow with 0x00 and 0x7F = COL127
#define OLED_CMD_SET_PAGE_RANGE 0x22       // can be used only in HORZ/VERT mode - follow with 0x00 and 0x07 = PAGE7

// Hardware Config (pg.31)
#define OLED_CMD_SET_DISPLAY_START_LINE 0x40
#define OLED_CMD_SET_SEGMENT_REMAP_NORMAL 0xA0  // column 0 mapped to seg0
#define OLED_CMD_SET_SEGMENT_REMAP_FLIPPED 0xA1 // column 127 mapped to seg0
#define OLED_CMD_SET_MUX_RATIO 0xA8             // follow with 0x3F = 64 MUX 0x1f 32 mux
#define OLED_CMD_SET_COM_SCAN_MODE_NORMAL 0xC0
#define OLED_CMD_SET_COM_SCAN_MODE_REMAPPED 0xC8
#define OLED_CMD_SET_DISPLAY_OFFSET 0xD3 // follow with 0x00
#define OLED_CMD_SET_COM_PIN_MAP 0xDA    // follow with 0x12
#define OLED_CMD_NOP 0xE3                // NOP

// Timing and Driving Scheme (pg.32)
#define OLED_CMD_SET_DISPLAY_CLK_DIV 0xD5 // follow with 0x80
#define OLED_CMD_SET_PRECHARGE 0xD9       // follow with 0xF1
#define OLED_CMD_SET_VCOMH_DESELCT 0xDB   // follow with 0x30

// Charge Pump (pg.62)
#define OLED_CMD_SET_CHARGE_PUMP 0x8D // follow with 0x14
//
//
#define OLED_CMD_VAL_MUX_RATIO_32 0x1F
#define OLED_CMD_VAL_MEMORY_ADDR_MODE_HORZ 0x00
#define OLED_CMD_VAL_DISPLAY_OFFSET_0 0x00
#define OLED_CMD_VAL_SET_CONTRAST_DEFAULT 0x7F

#define OLED_CMD_VAL_COM_PIN_MAP_SEQUENTIAL 0x02 // apparently better for 128x32
#define OLED_CMD_VAL_COM_PIN_MAP_ALTERNATIVE 0x12
#define OLED_CMD_VAL_COM_PIN_MAP_SEQUENTIAL_FLIPPED 0x22
#define OLED_CMD_VAL_COM_PIN_MAP_ALTERNATIVE_FLIPPED 0x32

#define OLED_CMD_VAL_SET_DISPLAY_CLK_DIV_DEFAULT 0x80
#define OLED_CMD_VAL_SET_CHARGE_PUMP_ENABLE 0x14

/* Software init flow
* Set Mux Ratio
* Set Display Offset
* Set display start line
* set segment remap
* set com output scan direction
* set com pins hardware config
* set contrast control 
* disable entire display on
* set normal display
* set osc freq
* enable charge pump reg
* display on
*/
#define OLED_DEFAULT_INIT_CMD_LIST {          \
    OLED_CONTROL_BYTE_CMD_STREAM,             \
    OLED_CMD_DISPLAY_OFF,                     \
    OLED_CMD_SET_MUX_RATIO,                   \
    OLED_CMD_VAL_MUX_RATIO_32,                \
    OLED_CMD_SET_DISPLAY_OFFSET,              \
    OLED_CMD_VAL_DISPLAY_OFFSET_0,            \
    OLED_CMD_SET_SEGMENT_REMAP_FLIPPED,       \
    OLED_CMD_SET_COM_SCAN_MODE_REMAPPED,      \
    OLED_CMD_SET_COM_PIN_MAP,                 \
    OLED_CMD_VAL_COM_PIN_MAP_SEQUENTIAL,      \
    OLED_CMD_SET_CONTRAST,                    \
    OLED_CMD_VAL_SET_CONTRAST_DEFAULT,        \
    OLED_CMD_DISPLAY_RAM,                     \
    OLED_CMD_DISPLAY_NORMAL,                  \
    OLED_CMD_SET_MEMORY_ADDR_MODE,            \
    OLED_CMD_VAL_MEMORY_ADDR_MODE_HORZ,       \
    OLED_CMD_SET_DISPLAY_CLK_DIV,             \
    OLED_CMD_VAL_SET_DISPLAY_CLK_DIV_DEFAULT, \
    OLED_CMD_SET_CHARGE_PUMP,                 \
    OLED_CMD_VAL_SET_CHARGE_PUMP_ENABLE,      \
    OLED_CMD_DISPLAY_ON,                      \
}

#define OLED_DEFAULT_INIT_CMD_LIST_LEN 21
