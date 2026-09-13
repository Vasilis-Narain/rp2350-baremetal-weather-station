#pragma once
#include <stdint.h>

typedef __SIZE_TYPE__ usize;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef i32 b32;

typedef float f32;
typedef double f64;

typedef struct {
    const char *ptr;
    u32 len;
} String;
#define LITERAL(lit) ((String){"" lit, sizeof(lit) - 1})
#define STRING(str, length) ((String){(str), (length)})

typedef struct {
    usize len;
    u8 *ptr;
} Buffer;
#define BUFFER(ptr, length) ((Buffer){(ptr), (length)})

_Static_assert(sizeof(u8) == 1 && sizeof(u16) == 2 && sizeof(u32) == 4 && sizeof(u64) == 8, "unsigned widths");
_Static_assert(sizeof(i8) == 1 && sizeof(i16) == 2 && sizeof(i32) == 4 && sizeof(i64) == 8, "signed widths");
_Static_assert((i8)-1 < 0 && (i32)-1 < 0, "signed types must be signed");
_Static_assert(sizeof(f32) == 4 && sizeof(f64) == 8, "float widths");

void *memcpy(void *dest, const void *src, usize n); // optionally provide own implementation
void *memset(void *blk, i32 c, usize n);            // optionally provide own implementation
usize strlen(const char *str);                      // optionally provide own implementation

#define WFI __asm__ volatile("WFI")
#define BARRIER __asm__ volatile("" ::: "memory")
extern void _DEFAULT_Handler();
#define PANIC _DEFAULT_Handler()
