/* ay2ym.h
 * Header for AY to YM emulator context.
 *
 * Copyright (c) 2025 Your Name
 *
 * This code is free, do whatever you want with it.
 */

#ifndef __AY2YM_INCLUDED__
#define __AY2YM_INCLUDED__

#include "z80emu.h"
#include <stdint.h>

 // AY to YM emulator context structure
typedef struct AY2YM {
    Z80_STATE state;          // Z80 CPU state
    uint8_t memory[0x10000];  // 64KB RAM
    uint8_t ay_reg_select;    // currently used for register latch
    uint8_t ay_regs[16];      // AY registers
    uint8_t beeper;           // beeper state (bit 4)
    uint8_t is_done;          // emulation done flag

    // Add this:
    uint8_t addr_latch;     // latched AY register index
} AY2YM;

#ifdef __cplusplus
extern "C" {
#endif

// System call handler for I/O traps
extern void SystemCall(AY2YM* ay2ym);

#ifdef __cplusplus
}
#endif

#endif