#define _CRT_SECURE_NO_WARNINGS

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
    //printf("SystemCall at PC=0x%04X, A=0x%02X\n", pc, a);

    if (pc == 0xFFFF) {
        ctx->is_done = 1;
    }
}

uint8_t ay2ym_in(void* context, uint16_t port, uint64_t elapsed_cycles) {
    AY2YM* ctx = (AY2YM*)context;
    uint8_t value = 0xFF;  // default value if no port matched

    if (port == 0xBFFD) {
        // Read from AY register selected by addr_latch
        value = ctx->ay_regs[ctx->addr_latch];
    }
    else if ((port & 0xFF) == 0xFE) {
        // Read beeper state (only low 8 bits of port matter here)
        value = ctx->beeper;
    }
    else {
        // Unknown port: handle system call or default input
        SystemCall(ctx);
        value = 0xFF;
    }

    return value;
}

void ay2ym_out(void* context, uint16_t port, uint8_t value, uint64_t elapsed_cycles) {
    AY2YM* ctx = (AY2YM*)context;

    if (port == 0xFFFD) {
        // Select AY register index (lower 4 bits only)
        ctx->addr_latch = value & 0x0F;
    }
    else if (port == 0xBFFD) {
        // Write value to selected AY register
        ctx->ay_regs[ctx->addr_latch] = value;
    }
    else if ((port & 0xFF) == 0xFE) {
        // Set beeper on/off depending on bit 4 of value
        ctx->beeper = (value & 0x10) ? 1 : 0;
    }

    ctx->is_done = 0;  // signal that something changed / needs updating
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

char* create_filename_from_song(const char* input_name, const char* song_name) {
    // Validate input strings
    if (!input_name || !song_name) return NULL;

    // Calculate required buffer size: input + " - " + song + ".ym" + null terminator
    size_t len = strlen(input_name) + 3 + strlen(song_name) + 3 + 1; // 3 for " - ", 3 for ".ym", 1 for '\0'

    // Allocate memory for the new filename string
    char* filename = (char*)malloc(len);
    if (!filename) return NULL;

    // Format the final string safely
    snprintf(filename, len, "%s - %s.ym", input_name, song_name);

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
        printf("\tCopying block addr=0x%04X length=0x%X from file offset=0x%lX\n", addr, length, (unsigned long)offset_abs);
        printf("\tSource bytes: ");
        for (int i = 0; i < 16 && i < length; i++) {
            printf("%02X ", file[offset_abs + i]);
        }
        printf("\n\tDest bytes:   ");
        for (int i = 0; i < 16 && i < length; i++) {
            printf("%02X ", ctx.memory[addr + i]);
        }
        printf("\n");

        printf("\tLoaded block at 0x%04X, length 0x%04X from offset 0x%04zX\n\n", addr, length, offset_abs);

        pos += 6;
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

    const uint64_t cpu_clock = 3500000ULL;
    const uint64_t int_tstates = cpu_clock / FRAME_RATE;
    uint64_t total_cycles = (uint64_t)(song_length + fade_length) * int_tstates;

    printf("Starting emulation for %llu cycles (~%.2fs)...\n\n",
        total_cycles, (double)total_cycles / cpu_clock);

    uint64_t cycles = 0;
    uint64_t next_frame = int_tstates;
    const int step_cycles = 100;
    int frame_number = 0;

    char* output_file = create_filename_from_song(orig_file_name, song_name);
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
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, "YM6!", 4, ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }

    if (append_bytes(&ym_data, &ym_size, &ym_capacity, "LeOnArD!", 8, ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }

    // Number of frames (valid VBLs)
    size_t frame_count_offset = ym_size;
    pack_uint32_be(0, packed);
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 4, ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }

    // Song attributes: 0x00 (non-interleaved) | 0x08 (AY-3-8910 compatible)
    uint32_t song_attributes = 0x08;
    pack_uint32_be(song_attributes, packed);
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 4, ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }

    // Number of digidrums
    pack_uint32_be(0, packed);
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 2, ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }

    // Master clock
    pack_uint32_be(YM_CLOCK, packed);
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 4, ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }

    // Player frequency
    pack_uint16_be(FRAME_RATE, packed);
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 2, ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }

    // VBL number to loop song
    pack_uint32_be(0, packed);
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 4, ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }

    // Additional data size (0)
    pack_uint16_be(0, packed);
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, packed, 2, ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }

    // Append song name + null terminator
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, song_name, strlen(song_name), ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }
    ym_data[ym_size++] = 0;    

    // Append author + null terminator
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, author, strlen(author), ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }
    ym_data[ym_size++] = 0;

    // Append comment + null terminator
    const char* comment = "Converted by Negative Charge(@negativecharge.bsky.social)";
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, comment, strlen(comment), ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }
    ym_data[ym_size++] = 0;

    // Emulation tone data buffer
    size_t tone_capacity = 1024;
    size_t tone_size = 0;
    unsigned char* tone_data = (unsigned char*)malloc(tone_capacity);
    if (!tone_data) {
        free(ym_data);
        fclose(ym_file);
        return;
    }
    memset(tone_data, 0, tone_capacity);  // Zero tone buffer for safety

    while (cycles < total_cycles && !ctx.is_done) {
        int elapsed = Z80Emulate(&cpu, step_cycles, &ctx);
        if (elapsed <= 0) break;
        cycles += elapsed;

        if (cycles >= next_frame) {
            if (cpu.iff1 == 1) Z80Interrupt(&cpu, 0, &ctx);

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

    // Append all tone data collected immediately after comment string
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, tone_data, tone_size, ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }

    // Append YM file terminator
    if (append_bytes(&ym_data, &ym_size, &ym_capacity, "End!", 4, ym_file) != 0) {
        free(ym_data);
        fclose(ym_file);
        return;
    }

    // Patch final frame count into ym_data at the stored offset BEFORE writing file
    pack_uint32_be(frame_number, packed);
    memcpy(ym_data + frame_count_offset, packed, 4);

    // Write entire buffer from start
    fseek(ym_file, 0, SEEK_SET);
    fwrite(ym_data, 1, ym_size, ym_file);

    fclose(ym_file);
    free(ym_data);
    free(tone_data);

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

    size_t p_song_structures_abs = 18 + p_song_structures;

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