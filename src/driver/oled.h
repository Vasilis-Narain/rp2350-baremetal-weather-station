#pragma once
#include <type_alias.h>
#include "i2c_state_machine.h"

// Build tx buffer from frame data
void oled_build_tx_buffer(const u8 *frame_buffer, u32 len);
