# Bare-Metal Weather Station Firmware (RP2350)

Bare-metal C firmware for the Pico 2 that reads a Bosch BME280 over an interrupt-driven I2C driver. No SDK runtime, no HAL, no libc. Using only the generated `register` and `struct` headers provided by the `pico-sdk`.

***Work in progress***: startup, RTT logging, the interrupt-driven I2C state machine (bulk read and write) and SysTick-scheduled BME280 readout all work on hardware, on either of the RP2350's two I2C controllers. Next: an OLED display on the second bus.

## Hardware

- Pico 2 (RP2350)
- Pico Debug Probe over SWD
- BME280 breakout (the one I used is Pimoroni)

## Build

```sh
make           # build/firmware.uf2
make flash     # probe-rs download + reset
make run       # flash and stream RTT
```

Point `SDK` at the `src` directory of a `pico-sdk` checkout: `make SDK=/path/to/pico-sdk/src`

Also, `make flash` and `make run` require [`probe-rs`](https://probe.rs/) to be on PATH and a connected debug probe.

## What's done

**Startup** (`entry.c`, `_crt0.c`, `link.ld`) -- vector table, all 52 IRQs weak-aliased to a handler that resets the peripherals and halts.
Clock is set to use the crystal oscillator for greater accuracy (`XOSC`) and then the `FPU` is enabled. Then `.data` is copied into RAM and `.bss` is zeroed.
Also includes the necessary boot block expected by the RP2350 (magic numbers pulled from the datasheet).

**RTT** (`rtt.c`) -- SEGGER's protocol is just a struct at a known layout in RAM that the host scans for, so, to keep with the zero-dependency spirit of this project,
I wrote the target side myself. The struct layout itself was taken from SEGGER's own documentation, but the ring buffer logic was written by me. Using `probe-rs` to attach
over SWD with the Pico Debug Probe. Also gave the SEGGER block dedicated memory space in `link.ld` called `.rtt_cb`.

**Writer** (`Writer.c`) -- buffered formatting, no `printf`. The idea was taken from Zig's `std.Io.Writer` interface: format into a caller-owned buffer and only do i/o on flush,
reducing i/o calls dramatically. In this case i/o is just copying bytes to the SEGGER buffers. As this has not been written to be conformant to `printf` I took several liberties
with the syntax (again inspired by Zig, but not faithfully). `{d}` for int, `{u}` for uint, `{u:xb}` for a (`x`)hex (`b`) byte. Decimal to string using a two-digit-at-a-time
lookup table based algorithm. Hex to string branchless SWAR algorithm, adapted from [here](https://johnnylee-sde.github.io/Fast-unsigned-integer-to-hex-string/).

**I2C** (`driver/`) -- ISR state machine with per-bus context, so both I2C controllers share the same ISR logic (both tested on hardware).
Includes an address probe used to scan the bus at startup. The read request is initiated by SysTick (on a 500ms interval) and the main loop
polls the bus state for completion to update output data. Currently the data is displayed via RTT for testing.
An ISR storm guard masks the controller's interrupts and faults the transfer (`I2C_FAULT_STORM`) after 10,000 interrupts in one transfer.
With `I2C_DEBUG` set to 1 (in `type_alias.h`) it also snapshots the interrupt/FIFO registers for printing over RTT.

## TODO

- verify the bus with a logic analyser.
- small I2C OLED display on the second bus.
