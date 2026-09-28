#include <type_alias.h>
#include <hardware/address_mapped.h>
#include <hardware/structs/clocks.h>
#include <hardware/regs/clocks.h>
#include <hardware/structs/xosc.h>
#include <hardware/structs/scb.h>
#include <hardware/structs/ticks.h>

#define TIMER0_CYCLES_FOR_1_US 12u

#define _config_timer(generator, cycles_to_tick)                                              \
    do {                                                                                      \
        ticks_hw->ticks[generator].ctrl = 0;                                                  \
        ticks_hw->ticks[generator].cycles = cycles_to_tick;                                   \
        ticks_hw->ticks[generator].ctrl = TICKS_TIMER0_CTRL_ENABLE_BITS;                      \
        while (!(ticks_hw->ticks[generator].ctrl & (1u << TICKS_TIMER0_CTRL_RUNNING_LSB))) {} \
    } while (0)

extern u8 __data_start[];
extern u8 __data_end[];
extern u8 __data_lma[];
extern u8 __bss_start[];
extern u8 __bss_end[];

static void _config_ref_clock();
static void _config_sys_clock();
static void _enable_fpu();

extern void main();

__attribute__((noreturn)) void _crt0() {
    _config_sys_clock();
    _config_ref_clock();
    _config_timer(TICK_TIMER0, TIMER0_CYCLES_FOR_1_US);
    _enable_fpu();

    // Copy data segment
    u8 *dest = __data_start;
    const u8 *src = __data_lma;
    while (dest < __data_end) {
        *dest++ = *src++;
    }

    // Clear bss segment
    u8 *bss = __bss_start;
    while (bss < __bss_end) {
        *bss++ = 0;
    }

    main();

    // in case main was made to return
    __asm__("CPSID I");
    for (;;) {
        __asm__("WFI");
    }
}

static void _config_ref_clock() {
    clocks_hw->clk[clk_ref].ctrl = (clocks_hw->clk[clk_ref].ctrl & ~CLOCKS_CLK_REF_CTRL_SRC_BITS) |
                                   CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC;

    clocks_hw->clk[clk_ref].div = 1 << 16;

    while ((clocks_hw->clk[clk_ref].selected & (1 << CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC)) == 0) {}
}
static void _config_sys_clock() {
    clocks_hw->resus.ctrl = 0;

    xosc_hw->ctrl = XOSC_CTRL_FREQ_RANGE_VALUE_1_15MHZ;
    xosc_hw->startup = 47;
    hw_set_bits(&xosc_hw->ctrl, XOSC_CTRL_ENABLE_VALUE_ENABLE << XOSC_CTRL_ENABLE_LSB);
    while (((xosc_hw->status) & XOSC_STATUS_STABLE_BITS) == 0) {}

    hw_clear_bits(&clocks_hw->clk[clk_sys].ctrl, CLOCKS_CLK_SYS_CTRL_SRC_BITS);
    while (!(clocks_hw->clk[clk_sys].selected & (1 << CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF))) {}
}

static void _enable_fpu() {
    scb_hw->cpacr |= M33_CPACR_CP10_BITS | M33_CPACR_CP11_BITS;

    __asm__ volatile("dsb; isb");
}
