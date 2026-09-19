#pragma once
#include <type_alias.h>
#include "Writer.h"

#ifndef RTT_WRITER_MAX_BUFFER_SIZE
#define RTT_WRITER_MAX_BUFFER_SIZE 512
#endif

#ifndef RTT_WRITE_CHANNEL
#define RTT_WRITE_CHANNEL 0
#endif
#ifndef RTT_READ_CHANNEL
#define RTT_READ_CHANNEL 0
#endif

#ifndef RTT_MAX_NUM_UP_BUFFERS
#define RTT_MAX_NUM_UP_BUFFERS (1)
#endif

#ifndef RTT_MAX_NUM_DOWN_BUFFERS
#define RTT_MAX_NUM_DOWN_BUFFERS (1)
#endif

#ifndef RTT_BUFFER_SIZE_UP
#define RTT_BUFFER_SIZE_UP (1024)
#endif

#ifndef RTT_BUFFER_SIZE_DOWN
#define RTT_BUFFER_SIZE_DOWN (32)
#endif

#define _CORE_HAS_RTT_ASM_SUPPORT (1)
#define _CORE_NEEDS_DMB (1)
#define RTT_DMB() __asm__ volatile("dmb sy\n" : : :)

#ifndef RTT_CPU_CACHE_LINE_SIZE
#define RTT_CPU_CACHE_LINE_SIZE (0)
#endif

#if RTT_CPU_CACHE_LINE_SIZE
#define RTT_ROUND_UP_2_CACHE_LINE_SIZE(NumBytes) (((NumBytes + RTTRTT_CPU_CACHE_LINE_SIZE - 1) / RTTRTT_CPU_CACHE_LINE_SIZE) * RTT_CPU_CACHE_LINE_SIZE
#else
#define RTT_ROUND_UP_2_CACHE_LINE_SIZE(NumBytes) (NumBytes)
#endif

#define RTT_CB_SIZE (16 + 4 + 4 + (RTT_MAX_NUM_UP_BUFFERS * 24) + (RTT_MAX_NUM_DOWN_BUFFERS * 24))
#define RTT_CB_PADDING (RTT_ROUND_UP_2_CACHE_LINE_SIZE(RTT_CB_SIZE) - RTT_CB_SIZE)

// Up ring buffer (T -> H)
typedef struct {
    const char *sName;
    char *pBuffer;
    u32 SizeOfBuffer;
    u32 WrOff;
    volatile u32 RdOff;
    u32 Flags;
} rtt_buffer_up_t;

// Down ring buffer (H -> T)
typedef struct {
    const char *sName;
    char *pBuffer;
    u32 SizeOfBuffer;
    volatile u32 WrOff;
    u32 RdOff;
    u32 Flags;
} rtt_buffer_down_t;

// RTT control block
typedef struct {
    char acID[16]; // Initialized to "SEGGER RTT"
    i32 MaxNumUpBufers;
    i32 MaxNumDownBufers;
    rtt_buffer_up_t aUp[RTT_MAX_NUM_UP_BUFFERS];
    rtt_buffer_down_t aDown[RTT_MAX_NUM_DOWN_BUFFERS];
#if RTT_CB_PADDING
    u8 aDummy[RTT_CB_PADDING];
#endif
} rtt_ctrl_block_t;

// Global data
extern rtt_ctrl_block_t _SEGGER_RTT;
extern u32 rtt_bytes_dropped;

u32 rtt_write(const char *str, u32 len, u8 channel);
u32 rtt_read(char *buf, u32 max, u8 channel);
void rtt_flush(Writer *writer);
