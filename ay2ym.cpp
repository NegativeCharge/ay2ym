#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Read signed 16-bit big-endian
static inline int16_t read_be16s(const uint8_t* ptr) {
    return (int16_t)((ptr[0] << 8) | ptr[1]);
}

// Read unsigned 16-bit big-endian
static inline uint16_t read_be16u(const uint8_t* ptr) {
    return (uint16_t)((ptr[0] << 8) | ptr[1]);
}

// Resolve a signed 16-bit relative pointer from 'pointer_pos'
size_t resolve_rel_pointer(const uint8_t* file, size_t size, size_t pointer_pos) {
    if (pointer_pos + 2 > size) return SIZE_MAX; // invalid

    int16_t rel = read_be16s(file + pointer_pos);
    int64_t abs_off = (int64_t)pointer_pos + (int64_t)rel;

    if (abs_off < 0 || (size_t)abs_off >= size) return SIZE_MAX;

    return (size_t)abs_off;
}

// Return a pointer to a null-terminated string inside file buffer at offset
const char* read_ntstring(const uint8_t* file, size_t size, size_t offset) {
    if (offset >= size) return "(invalid offset)";
    // No length check here; trust string ends inside file for now
    return (const char*)(file + offset);
}

void parse_points_data(const uint8_t* file, size_t size, size_t p_points_offset) {
    if (p_points_offset == SIZE_MAX || p_points_offset + 6 > size) {
        printf("\tNo valid points data\n");
        return;
    }
    uint16_t stack = read_be16u(file + p_points_offset);
    uint16_t init = read_be16u(file + p_points_offset + 2);
    uint16_t interrupt = read_be16u(file + p_points_offset + 4);

    printf("\tPoints data: stack=0x%04X init=0x%04X interrupt=0x%04X\n", stack, init, interrupt);
}

void parse_blocks_data(const uint8_t* file, size_t size, size_t p_addresses_offset) {
    if (p_addresses_offset == SIZE_MAX) {
        printf("\tNo blocks data\n");
        return;
    }
    size_t pos = p_addresses_offset;

    printf("\tBlocks data:\n");
    while (pos + 6 <= size) {
        uint16_t address = read_be16u(file + pos);
        if (address == 0) break; // zero address marks end of blocks
        uint16_t length = read_be16u(file + pos + 2);
        int16_t offset_rel = read_be16s(file + pos + 4);
        size_t offset_abs = (size_t)(pos + 4) + offset_rel;

        if (offset_abs >= size) {
            printf("\t\tInvalid block offset\n");
            break;
        }

        printf("\t\tAddress=0x%04X Length=%u Offset=0x%zX\n", address, length, offset_abs);

        pos += 6;
    }
}

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

    // Resolve relative pointers for points and addresses
    size_t p_points = resolve_rel_pointer(file, size, song_data_offset + 10);
    size_t p_addresses = resolve_rel_pointer(file, size, song_data_offset + 12);

    printf("\ta_chan=%d b_chan=%d c_chan=%d noise=%d\n", a_chan, b_chan, c_chan, noise);
    printf("\tsong_length=%d (%.2fs)\n", song_length, song_length / 50.0);
    printf("\tfade_length=%d (%.2fs)\n", fade_length, fade_length / 50.0);
    printf("\ttotal length=%.2fs\n", (song_length + fade_length) / 50.0);
    printf("\thi_reg=0x%02X lo_reg=0x%02X\n", hi_reg, lo_reg);
    printf("\tp_points=0x%zX p_addresses=0x%zX\n", p_points, p_addresses);

    parse_points_data(file, size, p_points);
    parse_blocks_data(file, size, p_addresses);
}

void parse_song_structure_table(const uint8_t* file, size_t size, size_t table_offset, int num_songs) {
    if (table_offset == SIZE_MAX) {
        printf("Invalid songs structure pointer\n");
        return;
    }

    size_t entry_size = 4; // 2 bytes p_song_name, 2 bytes p_song_data
    size_t table_size = (num_songs + 1) * entry_size;

    if (table_offset + table_size > size) {
        printf("Songs structure table exceeds file size\n");
        return;
    }

    for (int i = 0; i <= num_songs; i++) {
        size_t entry_pos = table_offset + i * entry_size;

        size_t song_name_ptr = resolve_rel_pointer(file, size, entry_pos);
        size_t song_data_ptr = resolve_rel_pointer(file, size, entry_pos + 2);

        const char* song_name = "(invalid)";
        if (song_name_ptr != SIZE_MAX) song_name = read_ntstring(file, size, song_name_ptr);

        printf("\nSong %d: %s\n", i, song_name);
        parse_song_data(file, size, song_data_ptr);
    }
}

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

    printf("file_version=%d\n", file_version);
    printf("player_version=%d\n", player_version);
    printf("p_special_player=0x%04X\n", (uint16_t)p_special_player);
    printf("p_author=0x%04X\n", (uint16_t)p_author);
    printf("p_misc=0x%04X\n", (uint16_t)p_misc);
    printf("num_songs=%d\n", num_songs);
    printf("first_song=%d\n", first_song);
    printf("p_song_structures=0x%04X\n", (uint16_t)p_song_structures);

    size_t p_author_abs = (size_t)12 + p_author;
    size_t p_misc_abs = (size_t)14 + p_misc;
    size_t p_song_structures_abs = (size_t)18 + p_song_structures;

    if (p_author_abs < size)
        printf("Author: %s\n", read_ntstring(file, size, p_author_abs));
    else
        printf("Invalid author pointer\n");

    if (p_misc_abs < size)
        printf("Misc: %s\n", read_ntstring(file, size, p_misc_abs));
    else
        printf("Invalid misc pointer\n");

    parse_song_structure_table(file, size, p_song_structures_abs, num_songs);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <ayfile>\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        perror("Failed to open file");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* file_data = (uint8_t*)malloc(size);
    if (!file_data) {
        perror("malloc failed");
        fclose(f);
        return 1;
    }

    if (fread(file_data, 1, size, f) != size) {
        perror("Failed to read file");
        free(file_data);
        fclose(f);
        return 1;
    }
    fclose(f);

    parse_ay_file(file_data, size);

    free(file_data);
    return 0;
}