# Bare-Metal Weather Station Firmware (RP2350)

Uses `register` and `struct` headers from the `pico-sdk`.
Doesn't link against libc.

Readings are taken and rendered every second but supports faster speeds by reducing  `MAIN_PERIOD_MS`.
Both devices sit on I2C1 (GP14/GP15), the driver serialises their transfers through a state machine.
A button cycles the display between pages signalled with different LED's.

## Hardware

- Pico 2 (RP2350)
- Pico Debug Probe over SWD
- BME280 breakout (Pimoroni)
- SSD1306 128x32 OLED
- 4-pin tactile button on GP18
- Indicator LEDs on GP19 (green), GP20 (red), GP21 (yellow)

Button network:

- 10k from 3V3 to node A
- button from node A to ground
- 5k from node A to GP18 (two 10k in parallel -- see the E9 note below)
- 100nF from GP18 to ground

## Build

- `make` -- builds `build/firmware.uf2`
- `make flash` -- `probe-rs download` + reset
- `make run` -- flash and stream RTT
- `make attach` -- stream RTT from an already-running target, no flash, no reset
- `make uf2 DRIVE=/d` -- copy the image to a mounted BOOTSEL volume, no probe needed

Notes:

- point `SDK` at the `src` directory of a `pico-sdk` checkout: `make SDK=/path/to/pico-sdk/src`
- `flash`, `run` and `attach` need [`probe-rs`](https://probe.rs/) on PATH and a connected debug probe

## Components

#### **Startup** (`entry.c`, `_crt0.c`, `link.ld`)
- `entry.c` defines the vector table with irq and default handlers.
  - also defines the boot block expected by the RP2350 *([datasheet section 5.9.5](https://pip-assets.raspberrypi.com/categories/1214-rp2350/documents/RP-008373-DS-2-rp2350-datasheet.pdf/#page=429))*.
- `_crt0.c` sets the clock to the crystal oscillator, enables the fpu, copies `.data` over and zeroes `.bss`.

#### **I2C** (`driver/i2c_state_machine.c`)
- ISR state machine with per-bus context
- Three transfer paths: bulk read, sequential/alternating CPU writes, and a DMA write
- Transfers are started asynchronously and the caller polls `i2c_poll_state()` 
- I2C bus recovery sequence on reset
- Tested on a usb logic analyser: `signals/`.

#### **Bus sharing**

The current app functions through a state machine:

```plantuml
@startuml
skinparam shadowing false
skinparam ArrowColor #444
skinparam ActivityBackgroundColor White
skinparam ActivityBorderColor #666
skinparam ActivityDiamondBackgroundColor White
skinparam ActivityDiamondBorderColor #666

|#FDF2DC|Button IRQs|
|#E8F1FB|Main loop|
|#EAF5EA|Bus IRQs|

|Main loop|
start
if () then
  |Button IRQs|
  :GPIO falling edge on GP18;
  if (quiet >= 30 ms\nand no confirm pending?) then (yes)
    :arm 20 ms confirm;
  else (no)
    :ignored as bounce;
    stop
  endif
  :SysTick, 20 ms later;
  if (GP18 still low?) then (yes)
    :ch_select++;
  else (no)
    :dropped as a glitch;
    stop
  endif
  detach
else ()
  |Main loop|
  repeat
    :APP_START_READ
    issue BME280 read;
    if () then
      :rasterize frame;
    else ()
      |Bus IRQs|
      :I2C ISR pumps TX, drains RX.
      A STOP sets bus DONE or ERROR;
    endif
    |Main loop|
    :APP_READING;
    if (read status?) then (DONE)
      :have_raw = TRUE;
    else (ERROR)
      :log_fault;
    endif
    :start OLED DMA;
    if () then
      if (have_raw?) then (yes)
        :compensate raw data;
      endif
    else ()
      |Bus IRQs|
      :DMA feeds IC_DATA_CMD
      STOP sets bus DONE;
    endif
    |Main loop|
    :APP_WRITING;
    if (frame sent OK?) then (no)
      :log_fault;
    endif
    :APP_IDLE, WFI;
  backward :SysTick IRQ
  ch_last != ch_select, or 1 s elapsed
  main_state = APP_START_READ;
  repeat while ()
endif
@enduml
```


SysTick sets the state, the main loop runs it, and both CPU-heavy steps are placed so they overlap a transfer instead of
stalling behind one. `APP_START_READ` issues the BME280 read and then, while that read is still on the wire, clears the framebuffer and
rasterizes the current page into it. `APP_READING` waits for the read to land, starts the OLED DMA, and only then runs the fixed-point
compensation, so that math overlaps the ~46ms frame write rather than delaying it. `APP_WRITING` waits for the frame out and drops back to
idle. Refresh is every second, or immediately when the page changes. One owner of the bus at a time, no locks, no allocation.

Ordering it that way has one consequence worth remembering: the frame drawn in a given cycle renders `last_data`, which is the previous
cycle's compensated values. The display trails the sensor by one refresh. A page switch shows the new label straight away, since the page
index is read at rasterize time, but the numbers under it are one cycle old.

**Page selection** (`main.c`) -- the button cycles through four pages and the matching LED says which one is showing. Both edges are enabled
on GP18. The handler timestamps every edge and ignores a falling edge arriving within 30ms of any other edge, which kills the bounce burst
on the press and on the release -- the release matters, because a bouncing release emits falling edges too, and they land long after any
lockout keyed to the press itself. A falling edge that survives that only arms a 20ms timer; SysTick re-reads the pin when it expires and
advances the page only if the line is still low. A bounce blip or a noise glitch cannot hold the line down for 20ms, so neither counts.
The confirm runs on the same SysTick tick that latches the page change, so the display picks it up on the next idle pass instead of waiting
for the one-second refresh.

**RP2350-E9** -- worth knowing about if you wire a button to this chip. With the input buffer enabled and the pad sitting between logic
levels, the pad leaks up to 120uA, and the erratum asks for 8.2k or less between the pin and whatever is pulling it low. My first attempt
used 10k there, and with the button held the pin measured 1.53V: never a valid low, so presses did nothing, and every bit of noise during
a display update chattered the edge detector instead. Dropping that resistor to 5k puts the held level around 0.6V and it behaves. The
pull-up side is unaffected, so the 10k from 3V3 stays as it is.

**OLED** (`driver/oled.c`) -- SSD1306 driver over the same bus. 512-byte page-major framebuffer with pixel, bitmap and text rasterizers.
The frame is stored as `u16` entries so the final I2C `STOP` bit can be encoded in the last word, which lets DMA push the whole frame
into `IC_DATA_CMD` without a completion IRQ or a polling loop to append the stop. Text position, font and inversion live in a single
module-level descriptor set by `oled_set_writer_desc()`, so `oled_flush` matches the plain `Writer` flush signature instead of carrying varargs.

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

- calculate the SCL high/low counts at compile time instead of hardcoding for 12MHz `clk_sys`.
