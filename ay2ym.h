/* ay2ym.h
 * Header for AY to YM emulator context.
  */

#ifndef __AY2YM_INCLUDED__
#define __AY2YM_INCLUDED__

#include "z80emu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#define strdup _strdup
#endif

#define YM_CLOCK 1773400      // ZX Spectrum Chip Frequency
#define FRAME_RATE 50
#define FRAME_COUNT_OFFSET 12

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

const char* orig_file_name = NULL;
const char* song_name = NULL;
const char* author = NULL;

#ifdef __cplusplus
extern "C" {
#endif

// System call handler for I/O traps
extern void SystemCall(AY2YM* ay2ym);

uint8_t ay2ym_in(void* context, uint16_t port, uint64_t elapsed_cycles);
void ay2ym_out(void* context, uint16_t port, uint8_t value, uint64_t elapsed_cycles);

#ifdef __cplusplus
}
#endif

#endif