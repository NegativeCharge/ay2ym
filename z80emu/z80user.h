/* z80user.h
 * Add your code here to interface the emulated system with z80emu. See towards
 * the end of the file for an example for running zextest.
 *
 * Copyright (c) 2016, 2017 Lin Ke-Fong
 *
 * This code is free, do whatever you want with it.
 */

#ifndef __Z80USER_INCLUDED__
#define __Z80USER_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

#include "..\ay2ym.h"
#include <stdio.h>

/* Memory access macros */
#define Z80_READ_BYTE(address, x)                                       \
{                                                                       \
        (x) = ((AY2YM *) context)->memory[(address) & 0xffff];          \
}

#define Z80_FETCH_BYTE(address, x)      Z80_READ_BYTE((address), (x))

#define Z80_READ_WORD(address, x)                                       \
{                                                                       \
    unsigned char *memory = ((AY2YM *) context)->memory;                \
    (x) = (uint16_t)(memory[(address) & 0xffff]                        \
         | (memory[((address) + 1) & 0xffff] << 8));                    \
}

#define Z80_FETCH_WORD(address, x)     Z80_READ_WORD((address), (x))

#define Z80_WRITE_BYTE(address, x)                                      \
{                                                                       \
    ((AY2YM *) context)->memory[(address) & 0xffff] = (uint8_t)(x);     \
}

#define Z80_WRITE_WORD(address, x)                                      \
{                                                                       \
    unsigned char *memory = ((AY2YM *) context)->memory;                \
    memory[(address) & 0xffff] = (uint8_t)(x);                         \
    memory[((address) + 1) & 0xffff] = (uint8_t)((x) >> 8);            \
}

#define Z80_READ_WORD_INTERRUPT(address, x)   Z80_READ_WORD((address), (x))

#define Z80_WRITE_WORD_INTERRUPT(address, x)  Z80_WRITE_WORD((address), (x))

/* Input/output macros */
#define Z80_INPUT_BYTE(port, x)                                         \
{                                                                       \
    AY2YM *ctx = (AY2YM *)context;                                      \
    if ((port) == 0xBFFD) {                                             \
        /* Read from AY register */                                     \
        (x) = ctx->ay_regs[ctx->addr_latch];                            \
    } else if ((port & 0xFF) == 0xFE) {                                \
        (x) = ctx->beeper;                                              \
    } else {                                                            \
        SystemCall(ctx);                                                \
        (x) = 0xFF; /* Default input for other ports */                 \
    }                                                                   \
}

#define Z80_OUTPUT_BYTE(port, x)                                        \
{                                                                       \
    AY2YM *ctx = (AY2YM *)context;                                      \
    printf("OUT port=0x%04X value=0x%02X\n", (port), (x));              \
    if ((port) == 0xFFFD) {                                             \
        /* Latch AY register number */                                  \
        ctx->addr_latch = (uint8_t)(x & 0x0F);                          \
    } else if ((port) == 0xBFFD) {                                      \
        /* Write value to selected AY register */                       \
        ctx->ay_regs[ctx->addr_latch] = (uint8_t)(x);                   \
        printf("[AY2YM] Reg[%02X] <= %02X\n", ctx->addr_latch, x);      \
    } else if ((port & 0xFF) == 0xFE) {                                \
        /* Update beeper bit 4 */                                       \
        ctx->beeper = (x & 0x10) ? 1 : 0;                               \
    }                                                                   \
    ctx->is_done = 0;                                                   \
}

#ifdef __cplusplus
}
#endif

#endif