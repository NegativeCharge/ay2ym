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

#define ZX_SPECTRUM_CLOCK 1773400      // ZX Spectrum Chip Frequency
#define AMSTRAD_CPC_CLOCK 1000000      // Amstrad CPC Chip Frequency
#define FRAME_RATE 50
#define FRAME_COUNT_OFFSET 12

// Max ports per system
#define MAX_PORTS 8
#define CPC_PORT_MASK 0x0B

struct MachinePortSet {
    const char* name;
    uint8_t out_ports[MAX_PORTS];
    size_t   out_port_count;
};

// ZX Spectrum AY ports
static const struct MachinePortSet spectrum_ports = {
    "ZX Spectrum",
    { 0xFD, 0xFF, 0xBB },
    3
};

// Amstrad CPC AY port high-byte masks
const uint8_t cpc_port_hi_masks[] = { 0xF4, 0xF6 };
const size_t cpc_port_hi_mask_count = 2;


// AY to YM emulator context structure
typedef enum {
    PSG_INACTIVE = 0,
    PSG_LATCH_ADDR = 1,
    PSG_WRITE = 2,
    PSG_READ = 3
} PSG_Mode;

typedef enum {
    MACHINE_UNKNOWN = 0,
    MACHINE_ZX_SPECTRUM,
    MACHINE_AMSTRAD_CPC
} MachineType;

typedef struct {
    MachineType detected;
    int spectrum_port_count;
    int cpc_port_count;
} MachineDetectionResult;

typedef struct AY2YM {
    Z80_STATE state;          // Z80 CPU state
    uint8_t memory[0x10000];  // 64KB RAM
    uint8_t ay_reg_select;    // currently used for register latch
    uint8_t ay_regs[16];      // AY registers
    uint8_t beeper;           // beeper state (bit 4)
    uint8_t is_done;          // emulation done flag
    uint8_t addr_latch;       // latched AY register index

    // CPC-specific state
    uint8_t CPCData;
    uint8_t CPCSwitch;
} AY2YM;

const char* output_file = NULL;
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