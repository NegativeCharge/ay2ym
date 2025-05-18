#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay2ym.h"
#include "z80emu.h"
#include "z80user.h"

// Global AY2YM context and CPU state
static AY2YM ctx;
void* context = &ctx;

static Z80_STATE cpu;

// System call handler
void SystemCall(AY2YM* ctx) {
    uint16_t pc = cpu.pc;
    uint8_t a = cpu.registers.byte[Z80_A];
    printf("SystemCall at PC=0x%04X, A=0x%02X\n", pc, a);

    if (pc == 0xFFFF) {
        ctx->is_done = 1;
    }
}

// Read signed 16-bit big-endian
static inline int16_t read_be16s(const uint8_t* ptr) {
    return (int16_t)((ptr[0] << 8) | ptr[1]);
}

// Read unsigned 16-bit big-endian
static inline uint16_t read_be16u(const uint8_t* ptr) {
    return (uint16_t)((ptr[0] << 8) | ptr[1]);
}

// Resolve signed 16-bit relative pointer from 'pointer_pos'
size_t resolve_rel_pointer(const uint8_t* file, size_t size, size_t pointer_pos) {
    if (pointer_pos + 2 > size) return SIZE_MAX;
    int16_t rel = read_be16s(file + pointer_pos);
    int64_t abs_off = (int64_t)pointer_pos + rel;
    if (abs_off < 0 || (size_t)abs_off >= size) return SIZE_MAX;
    return (size_t)abs_off;
}

// Null-terminated string reader
const char* read_ntstring(const uint8_t* file, size_t size, size_t offset) {
    if (offset >= size) return "(invalid)";
    return (const char*)(file + offset);
}

void dump_relative_pointer(const uint8_t* file, size_t size, size_t pointer_pos, const char* label) {
    if (pointer_pos + 2 > size) {
        printf("[!] %s at 0x%zX: out of bounds\n", label, pointer_pos);
        return;
    }

    uint8_t hi = file[pointer_pos];
    uint8_t lo = file[pointer_pos + 1];
    int16_t rel = (int16_t)((hi << 8) | lo);
    int64_t abs_off = (int64_t)pointer_pos + rel;

    printf("[REL PTR] %s: at 0x%04zX -> rel=0x%04X (%d) -> abs=0x%04llX\n",
        label, pointer_pos, (hi << 8) | lo, rel, abs_off);

    if (abs_off < 0 || (size_t)abs_off >= size)
        printf("   [X] Absolute address 0x%04llX out of bounds!\n", abs_off);
    else
        printf("   [O] Points to value: 0x%02X 0x%02X 0x%02X ...\n",
            file[abs_off], file[abs_off + 1], file[abs_off + 2]);
}

// Load memory blocks into ctx.memory
void load_blocks(const uint8_t* file, size_t size, size_t p_addresses_offset) {
    if (p_addresses_offset == SIZE_MAX) {
        printf("\tNo blocks data\n");
        return;
    }

    size_t pos = p_addresses_offset;
    while (pos + 6 <= size) {
        uint16_t addr = read_be16u(file + pos);
        if (addr == 0) break;

        uint16_t length = read_be16u(file + pos + 2);
        int16_t offset_rel = read_be16s(file + pos + 4);
        size_t offset_abs = pos + 4 + offset_rel;

        if (offset_abs + length > size) {
            printf("\tInvalid block offset or length\n");
            break;
        }

        if (addr + length > sizeof(ctx.memory)) {
            printf("\tBlock addr+length exceeds memory size\n");
            break;
        }

        memcpy(ctx.memory + addr, file + offset_abs, length);
        printf("Copying block addr=0x%04X length=0x%X from file offset=0x%lX\n", addr, length, (unsigned long)offset_abs);
        printf("Source bytes: ");
        for (int i = 0; i < 16 && i < length; i++) {
            printf("%02X ", file[offset_abs + i]);
        }
        printf("\nDest bytes:   ");
        for (int i = 0; i < 16 && i < length; i++) {
            printf("%02X ", ctx.memory[addr + i]);
        }
        printf("\n");

        printf("\tLoaded block at 0x%04X, length 0x%04X from offset 0x%04zX\n", addr, length, offset_abs);

        pos += 6;
    }
}
// Initialize CPU registers
static void setup_cpu(uint16_t stack, uint16_t init_pc, uint8_t hi_reg, uint8_t lo_reg) {
    Z80Reset(&cpu);
    cpu.pc = init_pc;
    cpu.registers.word[Z80_SP] = stack;

    cpu.im = 2;
    cpu.i = 3;
    cpu.r = 0;

    cpu.registers.byte[Z80_A] = hi_reg;
    cpu.registers.byte[Z80_F] = lo_reg;
    cpu.registers.byte[Z80_B] = hi_reg;
    cpu.registers.byte[Z80_C] = lo_reg;
    cpu.registers.byte[Z80_D] = hi_reg;
    cpu.registers.byte[Z80_E] = lo_reg;
    cpu.registers.byte[Z80_H] = hi_reg;
    cpu.registers.byte[Z80_L] = lo_reg;

    cpu.iff1 = 0;
    cpu.iff2 = 0;
}

// Run emulation
static const unsigned char intz[] = {
    0xf3,       /* di */
    0xcd, 0x00, 0x00, /* call init */
    0xed, 0x5e, /* loop: im 2 */
    0xfb,       /* ei */
    0x76,       /* halt */
    0x18, 0xfa  /* jr loop */
};

static const unsigned char intnz[] = {
    0xf3,       /* di */
    0xcd, 0x00, 0x00, /* call init */
    0xed, 0x56, /* loop: im 1 */
    0xfb,       /* ei */
    0x76,       /* halt */
    0xcd, 0x00, 0x00, /* call interrupt */
    0x18, 0xf7  /* jr loop */
};

static void setup_interrupt_handler(uint8_t* memory, uint16_t init_addr, uint16_t interrupt_addr) {
    if (interrupt_addr == 0) {
        memcpy(memory, intz, sizeof(intz));
        // Patch the call init address (bytes 2 and 3)
        memory[2] = init_addr & 0xFF;
        memory[3] = (init_addr >> 8) & 0xFF;
    }
    else {
        memcpy(memory, intnz, sizeof(intnz));
        // Patch the call init address (bytes 2 and 3)
        memory[2] = init_addr & 0xFF;
        memory[3] = (init_addr >> 8) & 0xFF;
        // Patch the call interrupt address (bytes 11 and 12)
        memory[11] = interrupt_addr & 0xFF;
        memory[12] = (interrupt_addr >> 8) & 0xFF;
    }
}

static void emulate_song(uint16_t stack, uint16_t init, uint16_t song_length, uint16_t fade_length, uint8_t hi_reg, uint8_t lo_reg, uint16_t interrupt_addr) {
    // Clear AY registers and state variables
    memset(ctx.ay_regs, 0, sizeof(ctx.ay_regs));
    ctx.ay_reg_select = 0;
    ctx.is_done = 0;

    // Clear RAM regions except where song code resides (0xC000+)
    memset(ctx.memory + 0x0000, 0xC9, 0x0100);     // Trap stray jumps to 0x0000-0x00FF
    memset(ctx.memory + 0x0100, 0xFF, 0x3F00);    // Fill 0x0100-0x3FFF with 0xFF (NOP or RST 38)
    memset(ctx.memory + 0x4000, 0x00, 0x8000);    // Clear RAM 0x4000-0xBFFF
    // Leave 0xC000-0xFFFF untouched — assumes loader has loaded code here!

    // Set up interrupt handler at 0x0000 according to interrupt_addr presence
    setup_interrupt_handler(ctx.memory, init, interrupt_addr);

    printf("Setting up CPU: stack=0x%04X init=0x%04X hi_reg=0x%02X lo_reg=0x%02X interrupt=0x%04X\n", stack, init, hi_reg, lo_reg, interrupt_addr);
    setup_cpu(stack, init, hi_reg, lo_reg);

    // Store init address at 0x0002/0x0003 (used by interrupt handler call)
    ctx.memory[0x0002] = (init) & 0xFF;
    ctx.memory[0x0003] = (init >> 8) & 0xFF;

    // Set interrupt flip-flops and mode
    cpu.iff1 = 1;
    cpu.iff2 = 1;
    cpu.im = 2;

    // Calculate total number of T-states for the song + fade length
    const uint64_t cpu_clock = 3500000ULL;
    const uint64_t int_tstates = cpu_clock / 50; // 50Hz interrupt rate
    uint64_t total_cycles = (uint64_t)(song_length + fade_length) * int_tstates;

    printf("Starting emulation for %llu cycles (~%.2fs)...\n",
        total_cycles, (double)total_cycles / cpu_clock);

    uint64_t cycles = 0;
    uint64_t next_frame = int_tstates;
    const int step_cycles = 100;
    int frame_number = 0;

    while (cycles < total_cycles && !ctx.is_done) {
        int elapsed = Z80Emulate(&cpu, step_cycles, &ctx);
        if (elapsed <= 0) break;
        cycles += elapsed;

        if (cycles >= next_frame) {
            Z80Interrupt(&cpu, 0, &ctx);

            printf("[%05d]", frame_number);
            for (int i = 0; i < 16; i++)
                printf(" [%02X]=0x%02X", i, ctx.ay_regs[i]);
            printf("\n");

            frame_number++;
            next_frame += int_tstates;
        }
    }

    printf("Emulation ended after %d frames, %llu cycles.\n", frame_number, cycles);
}

// Parse points data and emulate
void parse_points_data_and_emulate(const uint8_t* file, size_t size, size_t p_points_offset, size_t p_addresses_offset, uint8_t hi_reg, uint8_t lo_reg, uint16_t song_length, uint16_t fade_length) {
    if (p_points_offset == SIZE_MAX || p_points_offset + 6 > size) {
        printf("\tNo valid points data\n");
        return;
    }

    uint16_t stack = read_be16u(file + p_points_offset);
    uint16_t init = read_be16u(file + p_points_offset + 2);
    uint16_t interrupt = read_be16u(file + p_points_offset + 4);

    printf("\tPoints: stack=0x%04X init=0x%04X interrupt=0x%04X\n", stack, init, interrupt);

    load_blocks(file, size, p_addresses_offset);

    emulate_song(stack, init, song_length, fade_length, hi_reg, lo_reg, interrupt);
}

// Parse single song data
void parse_song_data(const uint8_t* file, size_t size, size_t song_data_offset) {
    if (song_data_offset == SIZE_MAX || song_data_offset + 14 > size) {
        printf("\tInvalid song data\n");
        return;
    }

    const uint8_t* sd = file + song_data_offset;

    uint8_t a_chan = sd[0];
    uint8_t b_chan = sd[1];
    uint8_t c_chan = sd[2];
    uint8_t noise = sd[3];
    uint16_t song_length = read_be16u(sd + 4);
    uint16_t fade_length = read_be16u(sd + 6);
    uint8_t hi_reg = sd[8];
    uint8_t lo_reg = sd[9];

    size_t p_points = resolve_rel_pointer(file, size, song_data_offset + 10);
    size_t p_addresses = resolve_rel_pointer(file, size, song_data_offset + 12);

    dump_relative_pointer(file, size, song_data_offset + 10, "p_points");
    dump_relative_pointer(file, size, song_data_offset + 12, "p_addresses");

    printf("\n\ta_chan=%d b_chan=%d c_chan=%d noise=%d\n", a_chan, b_chan, c_chan, noise);
    printf("\tsong_length=%d (%.2fs)\n", song_length, song_length / 50.0);
    printf("\tfade_length=%d (%.2fs)\n", fade_length, fade_length / 50.0);
    printf("\thi_reg=0x%02X lo_reg=0x%02X\n", hi_reg, lo_reg);
    printf("\tp_points=0x%zX p_addresses=0x%zX\n", p_points, p_addresses);

    parse_points_data_and_emulate(file, size, p_points, p_addresses, hi_reg, lo_reg, song_length, fade_length);
}

// Parse song structure table
void parse_song_structure_table(const uint8_t* file, size_t size, size_t table_offset, int num_songs) {
    if (table_offset == SIZE_MAX) {
        printf("Invalid songs structure pointer\n");
        return;
    }

    size_t entry_size = 4;
    size_t table_size = (num_songs + 1) * entry_size;

    if (table_offset + table_size > size) {
        printf("Song table exceeds file size\n");
        return;
    }

    for (int i = 0; i <= num_songs; i++) {
        size_t entry_pos = table_offset + i * entry_size;
        size_t song_name_ptr = resolve_rel_pointer(file, size, entry_pos);
        size_t song_data_ptr = resolve_rel_pointer(file, size, entry_pos + 2);

        const char* song_name = (song_name_ptr != SIZE_MAX) ? read_ntstring(file, size, song_name_ptr) : "(invalid)";
        printf("\nSong %d: %s\n", i, song_name);

        parse_song_data(file, size, song_data_ptr);
    }
}

// Parse top-level AY file structure
void parse_ay_file(const uint8_t* file, size_t size) {
    if (size < 20) {
        printf("File too small\n");
        return;
    }

    uint8_t file_version = file[8];
    uint8_t player_version = file[9];
    int16_t p_special_player = read_be16s(file + 10);
    int16_t p_author = read_be16s(file + 12);
    int16_t p_misc = read_be16s(file + 14);
    uint8_t num_songs = file[16];
    uint8_t first_song = file[17];
    int16_t p_song_structures = read_be16s(file + 18);

    printf("file_version=%d\nplayer_version=%d\nnum_songs=%d first_song=%d\n", file_version, player_version, num_songs, first_song);

    size_t p_author_abs = (size_t)12 + p_author;
    size_t p_misc_abs = (size_t)14 + p_misc;
    if (p_author_abs < size)
        printf("Author: %s\n", read_ntstring(file, size, p_author_abs));
    else
        printf("Invalid author pointer\n");

    if (p_misc_abs < size)
        printf("Misc: %s\n", read_ntstring(file, size, p_misc_abs));
    else
        printf("Invalid misc pointer\n");

    size_t p_song_structures_abs = 18 + p_song_structures;

    parse_song_structure_table(file, size, p_song_structures_abs, num_songs);
}

// Main program entry point
int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s file.ay\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        perror("Failed to open input file");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* file = (uint8_t*)malloc(size);
    if (!file) {
        fclose(f);
        printf("Failed to allocate memory\n");
        return 1;
    }

    fread(file, 1, size, f);
    fclose(f);

    parse_ay_file(file, size);
    free(file);
    return 0;
}