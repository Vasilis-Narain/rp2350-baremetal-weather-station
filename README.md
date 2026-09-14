# BME280 I2C Driver (RP2350)

BME280 driver in C for the Pico 2, bare metal. No SDK, no HAL, no libc. Using only the `register` and `struct` headers provided by the `pico-sdk`,
because transcribing addresses isn't particularly interesting.

***Work in progress***: setup and blocking reads work. The goal is to have this work as a non-blocking interrupt-based state machine. 

## Hardware

- Pico 2 (RP2350)
- Pico Debug Probe over SWD
- BME280 breakout (the one I used is Pimoroni)

## Current API usage
```c
/* after clearing appropriate reset bits, setting up GPIO pins, 
and configuring SysTick */

////// In setup phase; before main loop /////

//Enable i2c master and irq
i2c_init_master();
i2c_irq_enable(I2C1); // TODO: support I2C0

//Get compensation parameters (dig_t1..t3, dig_p1..p9, dig_h1..h5 on BME280 datasheet)
bme280_calib_t calib_params;
i32 calib_error = bme280_get_calib_params(&calib_params);

//Handle errors as needed. Compensation parameters are a hard dependency
//so it's appropriate to hang/crash
if (calib_error != 0) {
    //I like to dump the error and hang in this case. Use whatever
    //logging method you like.
    print(rtt_writer, "get_calib_params error: {d}\n", calib_error);
    flush(rtt_writer);
    PANIC;
}

//Struct to hold configuration data to write to the BME280.
//Here I'm setting Normal mode with oversampling and filter settings
//matching the datasheet recommendation for Weather monitoring (section 3.5.1).
//These flags are `u8` bitfields; refer to the datasheet on how to set 
//different configuration settings.
bme280_config_t config_data = {
    .config = BME280_DEFAULT_CONFIG,
    .ctrl_hum = BME280_DEFAULT_CTRL_HUM,
    .ctrl_meas = BME280_DEFAULT_CTRL_MEAS,
};

i32 config_error = bme280_set_config(config_data);

//Handle errors as above.
if (config_error != 0) {
    print(rtt_writer, "set_config error: {d}\n", config_error);
    flush(rtt_writer);
    PANIC;
}

//Needs a delay after changing from Sleep mode.
//This is required by BME280 as the first conversion is 
//almost always garbage.
delay(20); // 20 ms is more than enough

//Global to signal to SysTick Handler that we're 
//free to start reading
bme280_is_configged = TRUE;


//the main loop:
for (;;) {
    if (i2c1_state == I2C_DONE) {
        //Compensate the raw data
        bme280_final_data data = bme280_compensate_data(&calib_params, &raw_data);

        //Release I2C bus.
        i2c1_state = I2C_IDLE;
        print(rtt_writer, "\x1B[1A\rreadout: temp: {d}, press: {d}, hum: {d}\n", data.temp, data.press, data.hum);

    } else if (i2c1_state == I2C_ERROR) {
        //TODO(vasilis): make error logging better
        // Handle errors as wanted. I currently just have a few 
        // volatile globals. 
        print(rtt_writer, "write err fault={u:x} abrt={u:x}\n", i2c_get_fault(), i2c_get_abrt_source());
        print(rtt_writer, "hits={u} stat={u:x} mask={u:x}\n", dbg_isr_hits, dbg_intr_stat, dbg_intr_mask);
        print(rtt_writer, "state={u} rxflr={u} txflr={u}\n", dbg_state, dbg_rxflr, dbg_txflr);
        flush(rtt_writer);
        i2c1_state = I2C_IDLE;
        PANIC;
    }
    flush(rtt_writer);

    WFI; // aka __asm__ volatile("wfi"); wait-for-interrupt
}


/// SyStick Handler and globals:
static volatile bme280_raw_data_t raw_data;
static b32 bme280_is_configged = FALSE;
volatile u32 ms = 0;
volatile u32 next = 500;

void SYSTICK_Handler() {
    ms++;
    if ((i32)(ms - next) >= 0) {
        next += 500; // initiating a read every 500 ms (tunable)
        sio_hw->gpio_togl = 1 << PIN25; // led signalling Systick is working
        if (bme280_is_configged) {
            bme280_start_read_raw_data(&raw_data);
        }
    }
}

```

## Build

```sh
make           # build/firmware.uf2
make flash     # probe-rs download + reset
make run       # flash and stream RTT
```

Point `SDK` in the Makefile to the Pico headers: `SDK=/path/to/pico-sdk/src/rp2350/hardware_regs/include`

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

**I2C** (`driver/`) -- ISR completed and working as expected. The read request is initiated by SysTick (on a 500ms interval) and the main loop
checks for `i2c1_state == I2C_DONE` to update output data. Currently the data is displayed via RTT for testing.

## TODO

- verify the bus with a logic analyser.
- adapt/refactor ISR to use multiple i2c busses (RP2350 has two) in order to support a small i2c lcd display. Ideally minimal modifications
(past supporting the second bus) is needed as the current ISR is peripheral agnostic.
