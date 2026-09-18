# Bare-Metal Weather Station Firmware (RP2350)

Bare-metal C firmware for the Pico 2. A Bosch BME280 sensor and an SSD1306 OLED share one I2C bus, driven by an
interrupt-driven state machine I wrote against the datasheets. No SDK runtime, no HAL, no libc -- only the generated
`register` and `struct` headers from the `pico-sdk`.

Readings are taken every 500ms, compensated, rendered into a framebuffer and pushed to the display over DMA.
Both devices sit on I2C1 (GP14/GP15); the driver serialises their transfers, since sharing a bus is the point of I2C.

## Hardware

- Pico 2 (RP2350)
- Pico Debug Probe over SWD
- BME280 breakout (Pimoroni)
- SSD1306 128x32 OLED

## Build

```sh
make           # build/firmware.uf2
make flash     # probe-rs download + reset
make run       # flash and stream RTT
```

Point `SDK` at the `src` directory of a `pico-sdk` checkout: `make SDK=/path/to/pico-sdk/src`

`make flash` and `make run` require [`probe-rs`](https://probe.rs/) on PATH and a connected debug probe.
`make uf2 DRIVE=/d` copies the image to a BOOTSEL volume instead, no probe needed.

## Components

**Startup** (`entry.c`, `_crt0.c`, `link.ld`) -- vector table, all 52 IRQs weak-aliased to a handler that resets the peripherals and halts.
Clock is set to use the crystal oscillator for greater accuracy (`XOSC`) and then the `FPU` is enabled. Then `.data` is copied into RAM and `.bss` is zeroed.
Also includes the necessary boot block expected by the RP2350 (magic numbers pulled from the datasheet).

**I2C** (`driver/i2c_state_machine.c`) -- ISR state machine with per-bus context, so both controllers share the same ISR logic (both tested on hardware).
Three transfer paths: bulk read, sequential/alternating CPU writes, and a DMA write that feeds the TX FIFO directly. Transfers are started
asynchronously and the caller polls `i2c_poll_state()`, so the main loop never blocks on the bus. Bus recovery at init clocks out a stuck
slave before the controller is enabled. Faults are explicit (`I2C_FAULT_ABORT`, `_OVERRUN`, `_STORM`, `_EARLY_STOP`) and the abort source
register is captured for diagnosis; an ISR storm guard masks interrupts and faults the transfer after 10,000 interrupts in one transfer.
Includes an address probe used to scan the bus at startup. With `I2C_DEBUG` set to 1 (in `type_alias.h`) it snapshots the interrupt/FIFO
registers for printing over RTT.

**Bus sharing** -- SysTick sets a flag every 500ms; the main loop runs a small state machine that issues the BME280 read, waits for it,
then hands the bus to the OLED frame write and waits for that. One owner at a time, no locks, no allocation.

**OLED** (`driver/oled.c`) -- SSD1306 driver over the same bus. 512-byte page-major framebuffer with pixel, bitmap and text rasterizers.
The frame is stored as `u16` entries so the final I2C `STOP` bit can be encoded in the last word, which lets DMA push the whole frame
into `IC_DATA_CMD` without a completion IRQ or a polling loop to append the stop.

**Fonts** (`fonts.c`, `tools/font_convert.py`) -- three bitmap fonts stored in the SSD1306 page-major layout so glyph blitting is a
straight copy. The Python tool converts row-major font dumps into that layout, previews the glyphs as ASCII, and patches the generated
arrays and descriptors into `fonts.c`/`fonts.h`.

**Writer** (`Writer.c`) -- buffered formatting, no `printf`. The idea was taken from Zig's `std.Io.Writer` interface: format into a caller-owned buffer and only do i/o on flush,
reducing i/o calls dramatically. As this has not been written to be conformant to `printf` I took several liberties
with the syntax (again inspired by Zig, but not faithfully). `{d}` for int, `{u}` for uint, `{u:xb}` for a (`x`)hex (`b`) byte. Decimal to string using a two-digit-at-a-time
lookup table based algorithm. Hex to string branchless SWAR algorithm, adapted from [here](https://johnnylee-sde.github.io/Fast-unsigned-integer-to-hex-string/).
The same interface backs both sinks: one instance flushes to RTT, another rasterizes into the OLED framebuffer.

**RTT** (`rtt.c`) -- SEGGER's protocol is just a struct at a known layout in RAM that the host scans for, so, to keep with the zero-dependency spirit of this project,
I wrote the target side myself. The struct layout itself was taken from SEGGER's own documentation, but the ring buffer logic was written by me. Using `probe-rs` to attach
over SWD with the Pico Debug Probe. Also gave the SEGGER block dedicated memory space in `link.ld` called `.rtt_cb`.

## TODO

- verify the bus with a logic analyser.
- calculate the SCL high/low counts at compile time instead of hardcoding for 12MHz `clk_sys`.
