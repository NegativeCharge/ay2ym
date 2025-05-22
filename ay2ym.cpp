#define _CRT_SECURE_NO_WARNINGS

#include "ay2ym.h"
#include "z80emu.h"
#include "z80user.h"

// Global AY2YM context and CPU state
static AY2YM ctx;
void* context = &ctx;

static Z80_STATE cpu;

MachineDetectionResult result;

#include <stdio.h>
#include <stdlib.h>

void delete_file_if_exists(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (file) {
        fclose(file);
        if (remove(filename) == 0) {
            printf("[INFO] Deleted file: %s\n", filename);
        }
        else {
            perror("[ERROR] Failed to delete file");
        }
    }
    else {
        // File does not exist — no action needed
        printf("[INFO] File not found (no need to delete): %s\n", filename);
    }
}

// System call handler
void SystemCall(AY2YM* ctx) {
    uint16_t pc = cpu.pc;
    uint8_t a = cpu.registers.byte[Z80_A];

    if (pc == 0xFFFF) {
        ctx->is_done = 1;
    }
}

uint8_t ay2ym_in(void* context, uint16_t port, uint64_t elapsed_cycles) {
    AY2YM* ctx = (AY2YM*)context;
    uint8_t value = 0xFF;
    uint8_t port_hi_masked = (port >> 8) & CPC_PORT_MASK;

    // ZX Spectrum ports
    if (port == 0xBFFD) {
        value = ctx->ay_regs[ctx->addr_latch];
    }
    else if ((port & 0xFF) == 0xFE) {
        value = ctx->beeper;
    }
    // CPC ports reads for AY registers
    else if (port_hi_masked == (0xF5 & CPC_PORT_MASK) || port_hi_masked == (0xF7 & CPC_PORT_MASK)) {
        value = ctx->ay_regs[ctx->addr_latch];
    }
    else {
        SystemCall(ctx);
    }

    return value;
}

void ay2ym_out(void* context, uint16_t port, uint8_t value, uint64_t elapsed_cycles) {
    AY2YM* ctx = (AY2YM*)context;
    uint8_t port_hi = (port >> 8);
    uint8_t port_hi_masked = port_hi & CPC_PORT_MASK;

    // ZX Spectrum handling
    if (port == 0xFFFD) {
        ctx->addr_latch = value & 0x0F;
    }
    else if (port == 0xBFFD) {
        ctx->ay_regs[ctx->addr_latch] = value;
    }
    else if ((port & 0xFF) == 0xFE) {
        ctx->beeper = (value & 0x10) ? 1 : 0;
    }
    // CPC ports handling
    else if (port_hi_masked == (0xF4 & CPC_PORT_MASK)) {
        ctx->CPCData = value;
        // Here you might call CPCCheckPIO equivalent if needed
    }
    else if (port_hi_masked == (0xF6 & CPC_PORT_MASK)) {
        uint8_t masked_val = value & 0xC0;

        if (ctx->CPCSwitch == 0) {
            ctx->CPCSwitch = masked_val;
        }
        else if (masked_val == 0) {
            switch (ctx->CPCSwitch) {
            case 0xC0:
                ctx->addr_latch = ctx->CPCData & 0x0F;
                break;
            case 0x80:
                if (ctx->addr_latch < 14) {
                    uint8_t filtered_val;
                    switch (ctx->addr_latch) {
                    case 1:
                    case 3:
                    case 5:
                    case 13:
                        filtered_val = ctx->CPCData & 0x0F;
                        break;
                    case 6:
                    case 8:
                    case 9:
                    case 10:
                        filtered_val = ctx->CPCData & 0x1F;
                        break;
                    case 7:
                        filtered_val = ctx->CPCData & 0x3F;
                        break;
                    default:
                        filtered_val = ctx->CPCData;
                        break;
                    }
                    ctx->ay_regs[ctx->addr_latch] = filtered_val;
                }
                break;
            }
            ctx->CPCSwitch = 0;
        }
    }

    ctx->is_done = 0;
}

static void pack_uint32_be(uint32_t value, unsigned char* out) {
    out[0] = (value >> 24) & 0xFF;
    out[1] = (value >> 16) & 0xFF;
    out[2] = (value >> 8) & 0xFF;
    out[3] = value & 0xFF;
}

static void pack_uint16_be(uint16_t value, unsigned char* out) {
    out[0] = (value >> 8) & 0xFF;
    out[1] = value & 0xFF;
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

// Returns 0 on success, -1 on allocation failure
int append_bytes(
    unsigned char** p_data, size_t* p_size, size_t* p_capacity,
    const void* src, size_t length, FILE* file_to_close_on_fail)
{
    if (*p_size + length > *p_capacity) {
        size_t new_capacity = (*p_capacity) * 2;
        if (new_capacity < *p_size + length)
            new_capacity = *p_size + length;
        unsigned char* new_data = (unsigned char*)realloc(*p_data, new_capacity);
        if (!new_data) {
            free(*p_data);
            if (file_to_close_on_fail) fclose(file_to_close_on_fail);
            *p_data = nullptr;
            *p_capacity = 0;
            *p_size = 0;
            return -1;
        }
        *p_data = new_data;
        *p_capacity = new_capacity;
    }
    memcpy(*p_data + *p_size, src, length);
    *p_size += length;
    return 0;
}

// Sanitize a filename component by replacing invalid Windows chars with '_'
void sanitize_filename_part(const char* src, char* dest, size_t max_len) {
    const char* invalid_chars = "<>:\"/\\|?*";
    size_t i, j = 0;
    for (i = 0; src[i] != '\0' && j < max_len - 1; i++) {
        char c = src[i];
        // Replace invalid chars or control chars with underscore
        if ((unsigned char)c < 32 || strchr(invalid_chars, c)) {
            dest[j++] = '_';
        }
        else {
            dest[j++] = c;
        }
    }
    dest[j] = '\0';
}

char* create_filename_from_song(uint8_t index, const char* input_name, const char* song_name) {
    if (!input_name || !song_name) return NULL;

    // Find last path separator (either / or \)
    const char* last_slash1 = strrchr(input_name, '/');
    const char* last_slash2 = strrchr(input_name, '\\');
    const char* last_slash = last_slash1 > last_slash2 ? last_slash1 : last_slash2;

    size_t path_len = 0;
    if (last_slash) {
        path_len = (size_t)(last_slash - input_name) + 1;  // include the slash
    }

    // Extract original filename part
    const char* original_filename = last_slash ? last_slash + 1 : input_name;

    // Sanitize original filename
    size_t filename_len = strlen(original_filename);
    char* safe_filename = (char*)malloc(filename_len + 1);
    if (!safe_filename) return NULL;
    sanitize_filename_part(original_filename, safe_filename, filename_len + 1);

    // Sanitize song_name too
    size_t song_len = strlen(song_name);
    char* safe_song = (char*)malloc(song_len + 1);
    if (!safe_song) {
        free(safe_filename);
        return NULL;
    }
    sanitize_filename_part(song_name, safe_song, song_len + 1);

    // Construct final string: [path][safe_filename] - [XX] [safe_song].ym
    // Max 2 digits + space = 3 chars for index part
    size_t total_len = path_len + strlen(safe_filename) + 3 + 3 + strlen(safe_song) + 3 + 1;

    char* filename = (char*)malloc(total_len);
    if (!filename) {
        free(safe_filename);
        free(safe_song);
        return NULL;
    }

    // Copy path part if any
    if (path_len > 0) {
        memcpy(filename, input_name, path_len);
    }

    // Format filename: [safe_filename] - [XX] [safe_song].ym
    snprintf(filename + path_len, total_len - path_len,
        "%s - %02u %s.ym", safe_filename, index, safe_song);

    free(safe_filename);
    free(safe_song);

    return filename;
}

char* remove_file_extension(const char* filename) {
    // Find last '.' in the string
    const char* dot = strrchr(filename, '.');

    // If no dot found, or it's the first char (hidden files like .bashrc), return full name
    if (!dot || dot == filename)
        return strdup(filename);

    // Allocate space for the new string
    size_t len = dot - filename;
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;

    // Copy the part before the dot
    strncpy(result, filename, len);
    result[len] = '\0';

    return result;
}

static void dump_memory_range(const uint8_t* memory, uint16_t start, uint16_t end) {
    printf("Memory dump from 0x%04X to 0x%04X:\n", start, end);
    for (uint16_t addr = start; addr <= end; addr += 16) {
        printf("0x%04X: ", addr);
        for (int i = 0; i < 16 && (addr + i) <= end; i++) {
            printf("%02X ", memory[addr + i]);
        }
        printf("\n");
    }
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

// Port lookup utility
bool is_port_in_list(uint8_t port, const uint8_t* list, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (port == list[i]) return true;
    }
    return false;
}

// Main load_blocks and detection function
void load_blocks(const uint8_t* file, size_t size, uint16_t init, size_t p_addresses_offset) {
    result.detected = MACHINE_UNKNOWN;
    result.spectrum_port_count = 0;
    result.cpc_port_count = 0;
    int ula_port_count = 0;

    if (p_addresses_offset == SIZE_MAX) {
        printf("\tNo blocks data\n");
        return;
    }

    size_t pos = p_addresses_offset;
    uint16_t addr = 0;

    while (pos + 6 <= size) {
        addr = read_be16u(file + pos);
        if (addr == 0) break;

        uint16_t length = read_be16u(file + pos + 2);
        int16_t offset_rel = read_be16s(file + pos + 4);
        size_t offset_abs = pos + 4 + offset_rel;

        if ((uint32_t)addr + length > 65536) {
            length = 65536 - addr;
            printf("\tClamped length to 0x%X due to memory size\n", length);
        }

        if (offset_abs + length > size) {
            length = (size > offset_abs ? size - offset_abs : 0) > UINT16_MAX
                ? UINT16_MAX
                : (uint16_t)(size > offset_abs ? size - offset_abs : 0);
            printf("\tClamped length to 0x%X due to file size\n", length);
        }

        if (length == 0) {
            printf("\tZero length block after clamping, skipping\n");
            break;
        }

        memcpy(ctx.memory + addr, file + offset_abs, length);
        printf("\tCopying block addr=0x%04X length=0x%X from file offset=0x%lX\n\n",
            addr, length, (unsigned long)offset_abs);

        for (size_t i = 0; i + 3 < length; i++) {
            uint8_t opcode = ctx.memory[addr + i];
            uint8_t operand = ctx.memory[addr + i + 1];

            if (opcode == 0xED &&
                (operand == 0x41 || operand == 0x49 || operand == 0x51 ||
                    operand == 0x59 || operand == 0x61 || operand == 0x69 ||
                    operand == 0x79)) {

                uint16_t port = (ctx.memory[addr + i + 3] << 8) | ctx.memory[addr + i + 2];
                uint8_t port_hi = port >> 8;

                printf("\t[DBG] OUT (C),r opcode 0x%02X to 0x%04X at 0x%04zX\n",
                    operand, port, addr + i);

                bool detected = false;

                if ((port & 0xFF00) == 0xFD00) {
                    result.spectrum_port_count++;
                    printf("\t[DBG] Detected as ZX Spectrum AY port (OUT (C),r)\n");
                    detected = true;
                }
                else if (port_hi < 0xF0) {
                    uint16_t bbb = (port & 0x0E00) >> 9;
                    if (bbb <= 7) {
                        result.cpc_port_count++;
                        printf("\t[DBG] Detected as CPC 4MB extension port (bbb = %u)\n", bbb);
                        detected = true;
                    }
                }

                if (!detected) {
                    printf("\t[DBG] OUT (C),r to 0x%04X at 0x%04zX undetected\n", port, addr + i);
                }
            }

            if (opcode == 0xD3) {
                uint8_t port = ctx.memory[addr + i + 1];
                printf("\t[DBG] OUT (n),A to 0x%02X at 0x%04lX\n", port, (unsigned long)(addr + i));

                bool detected = false;

                if (port == 0xFD || port == 0xBB) {
                    result.spectrum_port_count++;
                    printf("\t[DBG] Detected as ZX Spectrum AY port\n");
                    detected = true;
                }

                if ((port & CPC_PORT_MASK) == (0xF4 & CPC_PORT_MASK) ||
                    (port & CPC_PORT_MASK) == (0xF6 & CPC_PORT_MASK)) {
                    result.cpc_port_count++;
                    printf("\t[DBG] Detected as CPC AY port (OUT n,A)\n");
                    detected = true;
                }

                if (port == 0xFE) {
                    ula_port_count++;
                    printf("\t[DBG] Detected as ZX Spectrum ULA port write (0xFE)\n");
                    detected = true;
                }

                if (!detected) {
                    printf("\t[DBG] OUT (n),A to 0x%02X at 0x%04lX undetected\n", port, (unsigned long)(addr + i));
                }
            }
        }

        pos += 6;
    }

    if (result.spectrum_port_count > result.cpc_port_count) {
        result.detected = MACHINE_ZX_SPECTRUM;
    }
    else if (result.cpc_port_count > result.spectrum_port_count) {
        result.detected = MACHINE_AMSTRAD_CPC;
    }
    else {
        // If there's a ZX Spectrum AY port at all, default to Spectrum
        if (result.spectrum_port_count > 0) {
            result.detected = MACHINE_ZX_SPECTRUM;
        }
        else if (result.cpc_port_count > 0) {
            result.detected = MACHINE_AMSTRAD_CPC;
        }
        else if (init >= 0xC000) {
            result.detected = MACHINE_ZX_SPECTRUM;
            printf("[DBG] Heuristic: init address 0x%04X suggests ZX Spectrum\n", init);
        }
        else if (init >= 0x8000 && init < 0xC000) {
            result.detected = MACHINE_AMSTRAD_CPC;
            printf("[DBG] Heuristic: init address 0x%04X suggests Amstrad CPC\n", init);
        }
        else {
            result.detected = MACHINE_UNKNOWN;
        }
    }

    printf("\nAmstrad CPC AY port count: %d\n", result.cpc_port_count);
    printf("ZX Spectrum AY port count: %d\n", result.spectrum_port_count);
    printf("ZX Spectrum ULA port writes: %d\n", ula_port_count);
    printf("Detected machine: %s\n\n",
        result.detected == MACHINE_ZX_SPECTRUM ? "ZX Spectrum" :
        result.detected == MACHINE_AMSTRAD_CPC ? "Amstrad CPC" :
        "Unknown");

    // Pure beeper track detection
    if (result.spectrum_port_count == 0 &&
        result.cpc_port_count == 0 &&
        ula_port_count > 0) {
        printf("[INFO] Pure beeper track detected.\n");
        result.detected = MACHINE_UNKNOWN;
    }
}

// Initialize CPU registers
static void setup_cpu(uint16_t stack, uint8_t hi_reg, uint8_t lo_reg) {
    Z80Reset(&cpu);
 
    cpu.pc = 0x000;
    cpu.i = 0x003;
    cpu.registers.word[Z80_SP] = stack;

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

static const unsigned char intz[] = {
    0xf3,       // di
    0xcd,0,0,   // call init (addr to be patched)
    0xed,0x5e,  // loop: im 2
    0xfb,       // ei
    0x76,       // halt
    0x18,0xfa   // jr loop (relative jump)
};

static const unsigned char intnz[] = {
    0xf3,       // di
    0xcd,0,0,   // call init (addr to be patched)
    0xed,0x56,  // loop: im 1
    0xfb,       // ei
    0x76,       // halt
    0xcd,0,0,   // call interrupt (addr to be patched)
    0x18,0xf7   // jr loop (relative jump)
};

static void setup_interrupt_handler(uint8_t* memory, uint16_t init_addr, uint16_t interrupt_addr) {
    if (interrupt_addr == 0) {
        // Use intz handler (no interrupt call)
        memcpy(&memory[0], intz, sizeof(intz));
        // Patch call init address at intz[2] and intz[3]
        memory[2] = init_addr & 0xFF;
        memory[3] = (init_addr >> 8) & 0xFF;
    }
    else {
        // Use intnz handler (with interrupt call)
        memcpy(&memory[0], intnz, sizeof(intnz));
        // Patch call init address at intnz[2] and intnz[3]
        memory[2] = init_addr & 0xFF;
        memory[3] = (init_addr >> 8) & 0xFF;
        // Patch call interrupt address at intnz[9] and intnz[10]
        memory[9] = interrupt_addr & 0xFF;
        memory[10] = (interrupt_addr >> 8) & 0xFF;
    }
}

static void emulate_song(
    uint16_t stack, uint16_t init, uint16_t song_length, uint16_t fade_length,
    uint8_t hi_reg, uint8_t lo_reg, uint16_t interrupt_addr)
{
    memset(ctx.ay_regs, 0, sizeof(ctx.ay_regs));
    ctx.ay_reg_select = 0;
    ctx.is_done = 0;

    setup_interrupt_handler(ctx.memory, init, interrupt_addr);

    printf("Setting up CPU: stack=0x%04X init=0x0000 hi_reg=0x%02X lo_reg=0x%02X interrupt=0x%04X\n",
        stack, hi_reg, lo_reg, interrupt_addr);

    setup_cpu(stack, hi_reg, lo_reg);

    const uint64_t cpu_clock = (result.detected == MACHINE_AMSTRAD_CPC) ? 4000000ULL : 3500000ULL;
	printf("CPU clock: %llu Hz\n", cpu_clock);

    const uint64_t int_tstates = cpu_clock / FRAME_RATE;
    uint64_t total_cycles = (uint64_t)(song_length + fade_length) * int_tstates;

    printf("Starting emulation for %llu cycles (~%.2fs)...\n\n",
        total_cycles, (double)total_cycles / cpu_clock);

    uint64_t cycles = 0;
    uint64_t next_frame = int_tstates;
    const int step_cycles = 100;
    int frame_number = 0;

    FILE* ym_file = fopen(output_file, "wb");
    if (NULL == ym_file) {
        printf("Can't open output file '%s'\n", output_file);
        return;
    }

    size_t ym_capacity = 65536;
    size_t ym_size = 0;
    unsigned char* ym_data = (unsigned char*)malloc(ym_capacity);
    if (!ym_data) {
        fclose(ym_file);
        return;
    }

    unsigned char packed[4];

    // Write YM6 file ID and check string
    append_bytes(&ym_data, &ym_size, &ym_capacity, "YM6!", 4, ym_file);
    append_bytes(&ym_data, &ym_size, &ym_capacity, "LeOnArD!", 8, ym_file);

    // Number of frames placeholder
    size_t frame_count_offset = ym_size;
    pack_uint32_be(0, packed);
    append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 4, ym_file);

    // Song attributes: 0x09 (interleaved | AY-compatible)
    uint32_t song_attributes = 0x09;
    pack_uint32_be(song_attributes, packed);
    append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 4, ym_file);

    // Number of digidrums
    pack_uint32_be(0, packed);
    append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 2, ym_file);

    // Master clock
    pack_uint32_be(result.detected == MACHINE_AMSTRAD_CPC ? AMSTRAD_CPC_CLOCK : ZX_SPECTRUM_CLOCK, packed);
	printf("Master clock: %u Hz\n", (unsigned int)(result.detected == MACHINE_AMSTRAD_CPC ? AMSTRAD_CPC_CLOCK : ZX_SPECTRUM_CLOCK));
    append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 4, ym_file);

    // Player frequency
    pack_uint16_be(FRAME_RATE, packed);
    append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 2, ym_file);

    // VBL loop position
    pack_uint32_be(0, packed);
    append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 4, ym_file);

    // Additional data size
    pack_uint16_be(0, packed);
    append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 2, ym_file);

    // Song name, author, comment
    append_bytes(&ym_data, &ym_size, &ym_capacity, song_name, strlen(song_name), ym_file);
    ym_data[ym_size++] = 0;
    append_bytes(&ym_data, &ym_size, &ym_capacity, author, strlen(author), ym_file);
    ym_data[ym_size++] = 0;
    const char* comment = "Converted by Negative Charge(@negativecharge.bsky.social)";
    append_bytes(&ym_data, &ym_size, &ym_capacity, comment, strlen(comment), ym_file);
    ym_data[ym_size++] = 0;

    // Tone data buffer
    size_t tone_capacity = 1024;
    size_t tone_size = 0;
    unsigned char* tone_data = (unsigned char*)malloc(tone_capacity);
    if (!tone_data) {
        free(ym_data);
        fclose(ym_file);
        return;
    }
    memset(tone_data, 0, tone_capacity);

    // Emulation loop
    while (cycles < total_cycles && !ctx.is_done) {
        int elapsed = Z80Emulate(&cpu, step_cycles, &ctx);
        if (elapsed <= 0) break;
        cycles += elapsed;

        if (cycles >= next_frame) {
            if (cpu.iff1 == 1) {
                Z80Interrupt(&cpu, 0, &ctx);
            }

            if (tone_size + 16 > tone_capacity) {
                tone_capacity *= 2;
                unsigned char* new_tone_data = (unsigned char*)realloc(tone_data, tone_capacity);
                if (!new_tone_data) {
                    free(tone_data);
                    free(ym_data);
                    fclose(ym_file);
                    return;
                }
                tone_data = new_tone_data;
            }

            for (int i = 0; i < 16; i++) {
                tone_data[tone_size++] = ctx.ay_regs[i];
            }

            frame_number++;
            next_frame += int_tstates;
        }
    }

    // If no frames were generated, delete and exit
    if (frame_number == 0) {
        printf("No frames generated during emulation; deleting output file.\n");
        fclose(ym_file);
        free(ym_data);
        free(tone_data);
        if (remove(output_file) != 0) {
            printf("Warning: Failed to delete output file '%s'\n", output_file);
        }
        return;
    }

    // Trim trailing zero frames
    int zero_frame_count = 0;
    for (int i = frame_number - 1; i >= 0; i--) {
        int all_zero = 1;
        for (int reg = 0; reg < 16; reg++) {
            if (tone_data[i * 16 + reg] != 0) {
                all_zero = 0;
                break;
            }
        }
        if (all_zero) zero_frame_count++;
        else break;
    }

    if (zero_frame_count > 0) {
        frame_number -= zero_frame_count;
        tone_size = frame_number * 16;
        printf("Trimmed %d trailing zero frames from output.\n", zero_frame_count);
    }
    else {
        printf("No trailing zero frames to trim.\n");
    }

    // If no frames remain after trimming, delete and exit
    if (frame_number == 0) {
        printf("No non-zero frames remain after trimming; deleting output file.\n");
        fclose(ym_file);
        free(ym_data);
        free(tone_data);
        if (remove(output_file) != 0) {
            printf("Warning: Failed to delete output file '%s'\n", output_file);
        }
        return;
    }

    // Interleave tone data
    size_t interleaved_size = frame_number * 16;
    unsigned char* interleaved_data = (unsigned char*)malloc(interleaved_size);
    if (!interleaved_data) {
        free(tone_data);
        free(ym_data);
        fclose(ym_file);
        return;
    }

    for (int reg = 0; reg < 16; reg++) {
        for (int f = 0; f < frame_number; f++) {
            interleaved_data[reg * frame_number + f] = tone_data[f * 16 + reg];
        }
    }

    // Append interleaved data
    append_bytes(&ym_data, &ym_size, &ym_capacity, interleaved_data, interleaved_size, ym_file);

    // Append terminator
    append_bytes(&ym_data, &ym_size, &ym_capacity, "End!", 4, ym_file);

    // Patch final frame count
    pack_uint32_be(frame_number, packed);
    memcpy(ym_data + frame_count_offset, packed, 4);

    // Write file
    fseek(ym_file, 0, SEEK_SET);
    fwrite(ym_data, 1, ym_size, ym_file);

    fclose(ym_file);
    free(ym_data);
    free(tone_data);
    free(interleaved_data);

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

    // Clear memory regions according to spec:
    memset(ctx.memory + 0x0000, 0xC9, 0x0100);     // 0x0000-0x00FF with 0xC9 (RET)
    memset(ctx.memory + 0x0100, 0xFF, 0x3F00);    // 0x0100-0x3FFF with 0xFF (RST 38h)
    memset(ctx.memory + 0x4000, 0x00, 0xC000);    // 0x4000-0xFFFF with 0x00

    // Set 0xFB (EI) at 0x0038 as required by spec
    ctx.memory[0x0038] = 0xFB;
    
    load_blocks(file, size, init, p_addresses_offset);
	if (result.detected == MACHINE_UNKNOWN) {
		printf("\tNo valid AY ports detected, skipping emulation.\n");
		delete_file_if_exists(output_file);

		return;
	}
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

    // Derive song length if missing
    if (song_length == 0) {
        printf("\tNo song length provided - attempting to count addresses at p_addresses\n");
        if (p_addresses != SIZE_MAX) {
            size_t count = 0;
            while (p_addresses + (count * 2) + 1 < size) {
                uint16_t addr = read_be16u(file + p_addresses + (count * 2));
                if (addr == 0x0000) break;
                count++;
                if (count > 15000) {  // sanity check: never let it run forever
                    printf("\tAddress table too long - aborting at 15000 frames.\n");
                    break;
                }
            }
            if (count >= 100) {  // if enough addresses to be a reasonable song
                if (count > UINT16_MAX) {
                   printf("\tWarning: count exceeds uint16_t range, truncating to UINT16_MAX.\n");
                   song_length = UINT16_MAX;
                } else {
                   song_length = static_cast<uint16_t>(count);
                }
                printf("\tDerived song_length=%zu (%.2fs)\n", count, count / 50.0);
            }
            else {
                printf("\tToo few addresses (%zu) - defaulting to 5 minutes (15000 frames)\n", count);
                song_length = 15000;
            }
        }
        else {
            printf("\tp_addresses invalid - defaulting to 5 minutes (15000 frames)\n");
            song_length = 15000;
        }
    }

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

        song_name = (song_name_ptr != SIZE_MAX) ? read_ntstring(file, size, song_name_ptr) : "(invalid)";
        printf("\nSong %d: %s\n", i, song_name);

        output_file = create_filename_from_song(i, orig_file_name, song_name);
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
    if (p_author_abs < size) {
        author = read_ntstring(file, size, p_author_abs);
        printf("Author: %s\n", author);
    }
    else
        printf("Invalid author pointer\n");

    if (p_misc_abs < size)
        printf("Misc: %s\n", read_ntstring(file, size, p_misc_abs));
    else
        printf("Invalid misc pointer\n");

    size_t p_song_structures_abs = static_cast<size_t>(18) + p_song_structures;

    parse_song_structure_table(file, size, p_song_structures_abs, num_songs);
}

// Main program entry point
int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s file.ay\n", argv[0]);
        return 1;
    }

    int arg = 1;
    FILE* f = fopen(argv[arg], "rb");
    if (!f) {
        perror("Failed to open input file");
        return 1;
    }

    orig_file_name = remove_file_extension(argv[arg]);

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