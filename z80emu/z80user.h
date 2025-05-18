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
    uint16_t full_port = ((state->registers.byte[Z80_B] << 8) | (port));\
    (x) = ay2ym_in(context, full_port, elapsed_cycles);                 \
}

#define Z80_OUTPUT_BYTE(port, x)                                        \
{                                                                       \
    uint16_t full_port = ((state->registers.byte[Z80_B] << 8) | (port));\
    ay2ym_out(context, full_port, (uint8_t)(x), elapsed_cycles);        \
}

#ifdef __cplusplus
}
#endif

#endif