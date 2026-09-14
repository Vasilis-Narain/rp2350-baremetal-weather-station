#include <type_alias.h>
#include <hardware/regs/resets.h>
#include <hardware/structs/resets.h>
#include <hardware/structs/m33.h>

extern void __stack_top();
extern __attribute__((noreturn)) void _crt0();

__attribute__((noreturn)) void _RESET_Handler() {
    _crt0();
}

__attribute__((noreturn)) void panic() {
    __asm__ volatile("cpsid i");

    // Throw i2c busses into reset
    hw_set_bits(&resets_hw->reset, RESETS_RESET_I2C0_BITS | RESETS_RESET_I2C1_BITS);

    //if debugger attached halt here
    if (m33_hw->dhcsr & M33_DHCSR_C_DEBUGEN_BITS) {
        __asm__ volatile("bkpt #0");
    }

    for (;;) {
        __asm__ volatile("nop");
    }
}

void _DEFAULT_Handler() {
    panic();
}

void __attribute__((weak, alias("_DEFAULT_Handler"))) NMI_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SYSTICK_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SVC_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) HARDFAULT_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PENDSV_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) TIMER0_IRQ_0_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) TIMER0_IRQ_1_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) TIMER0_IRQ_2_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) TIMER0_IRQ_3_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) TIMER1_IRQ_0_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) TIMER1_IRQ_1_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) TIMER1_IRQ_2_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) TIMER1_IRQ_3_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PWM_IRQ_WRAP_0_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PWM_IRQ_WRAP_1_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) DMA_IRQ_0_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) DMA_IRQ_1_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) DMA_IRQ_2_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) DMA_IRQ_3_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) USBCTRL_IRQ_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PIO0_IRQ_0_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PIO0_IRQ_1_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PIO1_IRQ_0_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PIO1_IRQ_1_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PIO2_IRQ_0_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PIO2_IRQ_1_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) IO_IRQ_BANK0_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) IO_IRQ_BANK0_NS_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) IO_IRQ_QSPI_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) IO_IRQ_QSPI_NS_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SIO_IRQ_FIFO_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SIO_IRQ_BELL_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SIO_IRQ_FIFO_NS_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SIO_IRQ_BELL_NS_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SIO_IRQ_MTIMECMP_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) CLOCKS_IRQ_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SPI0_IRQ_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SPI1_IRQ_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) UART0_IRQ_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) UART1_IRQ_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) ADC_IRQ_FIFO_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) I2C0_IRQ_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) I2C1_IRQ_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) OTP_IRQ_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) TRNG_IRQ_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PROC0_IRQ_CTI_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PROC1_IRQ_CTI_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PLL_SYS_IRQ_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) PLL_USB_IRQ_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) POWMAN_IRQ_POW_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) POWMAN_IRQ_TIMER_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SPARE_IRQ_0_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SPARE_IRQ_1_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SPARE_IRQ_2_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SPARE_IRQ_3_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SPARE_IRQ_4_Handler();
void __attribute__((weak, alias("_DEFAULT_Handler"))) SPARE_IRQ_5_Handler();

#define RESERVED ((void (*)())0x44565352) // ASCII RSVD

void (*const _InterruptVector[])() __attribute__((section(".vector_table"))) = {
    &__stack_top,
    _RESET_Handler,
    NMI_Handler,       // NMI
    HARDFAULT_Handler, // HardFault
    RESERVED,
    RESERVED,
    RESERVED,
    RESERVED,
    RESERVED,
    RESERVED,
    RESERVED,
    SVC_Handler, // SVC
    RESERVED,
    RESERVED,
    PENDSV_Handler,           // PendSV
    SYSTICK_Handler,          // SysTick
    TIMER0_IRQ_0_Handler,     // IRQ0
    TIMER0_IRQ_1_Handler,     // IRQ1
    TIMER0_IRQ_2_Handler,     // IRQ2
    TIMER0_IRQ_3_Handler,     // IRQ3
    TIMER1_IRQ_0_Handler,     // IRQ4
    TIMER1_IRQ_1_Handler,     // IRQ5
    TIMER1_IRQ_2_Handler,     // IRQ6
    TIMER1_IRQ_3_Handler,     // IRQ7
    PWM_IRQ_WRAP_0_Handler,   // IRQ8
    PWM_IRQ_WRAP_1_Handler,   // IRQ9
    DMA_IRQ_0_Handler,        // IRQ10
    DMA_IRQ_1_Handler,        // IRQ11
    DMA_IRQ_2_Handler,        // IRQ12
    DMA_IRQ_3_Handler,        // IRQ13
    USBCTRL_IRQ_Handler,      // IRQ14
    PIO0_IRQ_0_Handler,       // IRQ15
    PIO0_IRQ_1_Handler,       // IRQ16
    PIO1_IRQ_0_Handler,       // IRQ17
    PIO1_IRQ_1_Handler,       // IRQ18
    PIO2_IRQ_0_Handler,       // IRQ19
    PIO2_IRQ_1_Handler,       // IRQ20
    IO_IRQ_BANK0_Handler,     // IRQ21
    IO_IRQ_BANK0_NS_Handler,  // IRQ22
    IO_IRQ_QSPI_Handler,      // IRQ23
    IO_IRQ_QSPI_NS_Handler,   // IRQ24
    SIO_IRQ_FIFO_Handler,     // IRQ25
    SIO_IRQ_BELL_Handler,     // IRQ26
    SIO_IRQ_FIFO_NS_Handler,  // IRQ27
    SIO_IRQ_BELL_NS_Handler,  // IRQ28
    SIO_IRQ_MTIMECMP_Handler, // IRQ29
    CLOCKS_IRQ_Handler,       // IRQ30
    SPI0_IRQ_Handler,         // IRQ31
    SPI1_IRQ_Handler,         // IRQ32
    UART0_IRQ_Handler,        // IRQ33
    UART1_IRQ_Handler,        // IRQ34
    ADC_IRQ_FIFO_Handler,     // IRQ35
    I2C0_IRQ_Handler,         // IRQ36
    I2C1_IRQ_Handler,         // IRQ37
    OTP_IRQ_Handler,          // IRQ38
    TRNG_IRQ_Handler,         // IRQ39
    PROC0_IRQ_CTI_Handler,    // IRQ40
    PROC1_IRQ_CTI_Handler,    // IRQ41
    PLL_SYS_IRQ_Handler,      // IRQ42
    PLL_USB_IRQ_Handler,      // IRQ43
    POWMAN_IRQ_POW_Handler,   // IRQ44
    POWMAN_IRQ_TIMER_Handler, // IRQ45
    SPARE_IRQ_0_Handler,      // IRQ46
    SPARE_IRQ_1_Handler,      // IRQ47
    SPARE_IRQ_2_Handler,      // IRQ48
    SPARE_IRQ_3_Handler,      // IRQ49
    SPARE_IRQ_4_Handler,      // IRQ50
    SPARE_IRQ_5_Handler,      // IRQ51
};

static const u32 __attribute__((used, section(".boot_block"))) __boot_block[5] = {
    0xFFFFDED3, // PICOBIN_BLOCK_MARKER_START
    0x10210142, // IMAGE_DEF: EXE, ARM, secure
    0x000001FF, // last-item terminator
    0x00000000, // next-block offset (0 = self-loop)
    0xAB123579, // PICOBIN_BLOCK_MARKER_END
};
