#include <type_alias.h>
#include "Writer.h"

//Error codes
#define WRITER_STRING_TOO_BIG -1
#define WRITER_BUFFER_WAS_FLUSHED 1

static u16 spread16(u8 num);
static u32 spread32(u16 num);
static void copy16(char *out, u16 num);
static void copy32(char *out, u32 num);
static u32 to_hex(int_size size, u32 value, char *out);
static u32 u32_to_string(u32 num, char *buff);
static u32 i32_to_string(i32 num, char *buff);
static i32 sign_extend(int_size size, i32 num);
static i32 print_int_hex(Writer *writer, int_size size, u32 num);
static i32 print_int_dec(Writer *writer, i32 num);
static i32 print_uint_dec(Writer *writer, u32 num);
static char next(const char **fmt, const char *end);
static u32 unsigned_trunc(int_size size, u32 num);

void writer_init(Writer *writer, char *buf, u32 capacity, void (*flush_fn)(Writer *)) {
    if (!writer || !buf || !capacity || !flush_fn) {
        PANIC;
    }
    writer->buf = buf;
    writer->capacity = capacity;
    writer->current_size = 0;
    writer->flush_fn = flush_fn;
}

void flush(Writer *writer) {
    if (!writer || !writer->buf || !writer->capacity || !writer->flush_fn) {
        PANIC;
    }
    writer->flush_fn(writer);
    writer->current_size = 0;
}

i32 writer_write(Writer *writer, const char *str, u32 length) {
    u32 remaining = length;

    while (remaining) {
        if (writer->current_size == writer->capacity) {
            flush(writer);
        }
        u32 space = writer->capacity - writer->current_size;
        u32 bytes_to_process = (remaining < space) ? remaining : space;

        memcpy(writer->buf + writer->current_size, str, bytes_to_process);
        writer->current_size += bytes_to_process;
        str += bytes_to_process;
        remaining -= bytes_to_process;
    }

    return (i32)length;
}

i32 writer_print(Writer *writer, const char *fmt, u32 length, ...) {
    if (length >= writer->capacity) {
        return WRITER_STRING_TOO_BIG;
    }

    const char *end = fmt + length;
    va_list args;
    i32 bytes_printed = 0;

    va_start(args, length);

    while (fmt < end) {
        char c = *fmt;

        if (c == '{') {
            c = next(&fmt, end);

            if (c == 's') { // This is the preferred path. Use LITERAL("hello daddy") or STRING(ptr, length) macros
                if (next(&fmt, end) != '}') {
                    PANIC;
                }

                String str = va_arg(args, String);
                i32 bytes = writer_write(writer, str.ptr, str.len);
                if (bytes < 0) {
                    PANIC;
                }
                bytes_printed += bytes;

            } else if (c == 'z') { // for c strings. We(I) don't like these.
                if (next(&fmt, end) != '}') {
                    PANIC;
                }
                char *str = va_arg(args, char *);
                i32 bytes = writer_write(writer, str, strlen(str));
                if (bytes < 0) {
                    PANIC;
                }
                bytes_printed += bytes;
            } else {
                fmt_int fmt_type = FMT_DEC;
                signedness sign;
                int_size size = WORD;
                if (c == 'd') {
                    sign = SIGNED;
                } else if (c == 'u') {
                    sign = UNSIGNED;
                } else {
                    PANIC;
                }

                c = next(&fmt, end);
                if (c == ':') {
                    c = next(&fmt, end);
                    if (c == 'x') {
                        fmt_type = FMT_HEX;
                        c = next(&fmt, end);
                    }

                    if (c == '}') {
                    } else if (c == 'b') {
                        size = BYTE;
                        c = next(&fmt, end);
                    } else if (c == 's') {
                        size = HALF;
                        c = next(&fmt, end);
                    } else {
                        PANIC;
                    }
                }
                if (c != '}') {
                    PANIC;
                }

                switch (fmt_type) {
                case FMT_DEC:
                    switch (sign) {
                    case UNSIGNED: {
                        u32 num = va_arg(args, u32);
                        if (size != WORD) {
                            num = unsigned_trunc(size, num);
                        }
                        i32 bytes = print_uint_dec(writer, num);
                        if (bytes < 0) {
                            PANIC;
                        }
                        bytes_printed += bytes;
                        break;
                    }
                    case SIGNED: {
                        i32 num = va_arg(args, i32);
                        if (size != WORD) {
                            num = sign_extend(size, num);
                        }
                        i32 bytes = print_int_dec(writer, num);
                        if (bytes < 0) {
                            PANIC;
                        }
                        bytes_printed += bytes;
                        break;
                    }
                    }
                    break;
                case FMT_HEX: {
                    u32 num = va_arg(args, u32);
                    i32 bytes = print_int_hex(writer, size, num);
                    if (bytes < 0) {
                        PANIC;
                    }
                    bytes_printed += bytes;
                    break;
                }
                }
            }
        } else {
            writer_write_char(writer, c);
            bytes_printed++;
        }
        fmt++;
    }
    va_end(args);
    return bytes_printed;
}

static char next(const char **fmt, const char *end) {
    (*fmt)++;
    return (*fmt < end) ? **fmt : '\0';
}

static i32 sign_extend(int_size size, i32 num) {
    i32 x;
    switch (size) {
    case BYTE:
        x = (i32)(i8)(u8)num;
        break;
    case HALF:
        x = (i32)(i16)(u16)num;
        break;
    case WORD:
        x = num;
        break;
    }
    return x;
}

static u32 unsigned_trunc(int_size size, u32 num) {
    u32 x;
    switch (size) {
    case BYTE:
        x = (u32)(u8)num;
        break;
    case HALF:
        x = (u32)(u16)num;
        break;
    case WORD:
        x = num;
        break;
    }
    return x;
}

static i32 print_uint_dec(Writer *writer, u32 num) {
    char buff[25];
    u32 size = 0;
    size = u32_to_string(num, buff);
    return writer_write(writer, (const char *)buff, size);
}

static i32 print_int_dec(Writer *writer, i32 num) {
    char buff[25];
    u32 size = 0;
    size = i32_to_string(num, buff);
    return writer_write(writer, (const char *)buff, size);
}

static i32 print_int_hex(Writer *writer, int_size size, u32 num) {
    char buff[12];
    u32 bytes = to_hex(size, num, buff);
    return writer_write(writer, (const char *)buff, bytes);
}

void writer_write_char(Writer *writer, char c) {
    if (writer->current_size >= writer->capacity) {
        flush(writer);
    }
    writer->buf[writer->current_size] = c;
    writer->current_size++;
}

static const char char_table[201] = {
    "00010203040506070809"
    "10111213141516171819"
    "20212223242526272829"
    "30313233343536373839"
    "40414243444546474849"
    "50515253545556575859"
    "60616263646566676869"
    "70717273747576777879"
    "80818283848586878889"
    "90919293949596979899",
};

static const u32 pow_of_10_table[10] = {
    1u,
    10u,
    100u,
    1000u,
    10000u,
    100000u,
    1000000u,
    10000000u,
    100000000u,
    1000000000u,
};

static u32 count_digits_u32(u32 num) {
    // minimum digits is 1 (0 has 1 digit)
    u32 result = 1;

    // u32 cannot be greater than 10 digits. Handles num > 1e9
    while (result < 10 && num >= pow_of_10_table[result]) {
        result++;
    }

    return result;
}

static u32 u32_to_string(u32 num, char *buff) {
    u32 initial_length = count_digits_u32(num);
    u32 length = initial_length;

    u32 i = 0;
    while (length >= 2) {
        length -= 2;
        u32 digits = num / pow_of_10_table[length];
        u32 index = digits * 2;
        buff[i] = char_table[index];
        buff[i + 1] = char_table[index + 1];
        num -= digits * pow_of_10_table[length];
        i += 2;
    }
    if (length == 1) {
        buff[i] = '0' + num;
    }

    return initial_length;
}

static u32 i32_to_string(i32 num, char *buff) {
    u32 size = 0;
    u32 mag;

    if (num < 0) {
        *buff++ = '-';
        size++;
        mag = 0u - (u32)num;
    } else {
        mag = (u32)num;
    }
    size += u32_to_string(mag, buff);
    return size;
}

static u32 to_hex(int_size size, u32 value, char *out) {
    u32 bytes_processed = 0;
    out[0] = '0';
    out[1] = 'x';
    switch (size) {
    case BYTE:
        copy16(out + 2, spread16((u8)value));
        bytes_processed = 4;
        break;
    case HALF:
        copy32(out + 2, spread32((u16)value));
        bytes_processed = 6;
        break;
    case WORD:
        copy32(out + 2, spread32((u16)(value >> 16)));
        copy32(out + 6, spread32((u16)value));
        bytes_processed = 10;
        break;
    }
    return bytes_processed;
}

// Following functions are to convert a uint*_t to a hex string.
//
// Modified from https://johnnylee-sde.github.io/Fast-unsigned-integer-to-hex-string/
//
static u32 spread32(u16 num) {
    u32 x = num;
    x = ((x & 0x00FF) << 16) | ((x & 0xFF00) >> 8);
    x = ((x & 0x00F000F0) >> 4) | ((x & 0x000F000F) << 8);

    u32 m = ((x + 0x06060606) >> 4) & 0x01010101;
    return x + 0x30303030 + m * 39; // 0x30 = '0'
}

static u16 spread16(u8 num) {
    u16 x = num;
    x = ((x & 0xF) << 8) | ((x & 0xF0) >> 4);

    u32 m = ((x + 0x0606) >> 4) & 0x0101;
    return x + 0x3030 + m * 39;
}

static void copy32(char *out, u32 num) {
    out[0] = (char)(num >> 0);
    out[1] = (char)(num >> 8);
    out[2] = (char)(num >> 16);
    out[3] = (char)(num >> 24);
}

static void copy16(char *out, u16 num) {
    out[0] = (char)(num >> 0);
    out[1] = (char)(num >> 8);
}
