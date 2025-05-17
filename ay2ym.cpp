#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STR_LEN 128
#define MAX_BLOCKS 256

#define PAUTHOR_OFFSET         12
#define PMISC_OFFSET           14
#define PSONGSSTRUCTURE_OFFSET 18

typedef struct {
    char fileID[5];
    char typeID[5];
    uint8_t fileVersion;
    uint8_t playerVersion;
    uint16_t pPlayer;
    uint16_t pAuthor;
    uint16_t pMisc;
    uint8_t numSongs;
    uint8_t firstSong;
    uint16_t pSongsStructure;
} AYHeader;

uint16_t read_be_uint16(FILE* f) {
    int hi = fgetc(f);
    int lo = fgetc(f);
    if (hi == EOF || lo == EOF) {
        fprintf(stderr, "Unexpected EOF while reading uint16\n");
        exit(1);
    }
    return ((uint16_t)hi << 8) | (uint16_t)lo;
}

void read_str_at(FILE* f, long offset, char* dest, size_t maxLen) {
    if (fseek(f, offset, SEEK_SET) != 0) {
        perror("Failed to seek in file");
        dest[0] = '\0';
        return;
    }
    size_t i = 0;
    int c;
    while (i < maxLen - 1 && (c = fgetc(f)) != EOF && c != 0) {
        dest[i++] = (char)c;
    }
    dest[i] = '\0';
}

void list_songs_and_blocks(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        perror("Error opening file");
        exit(1);
    }

    AYHeader header;

    if (fread(header.fileID, 1, 4, f) != 4) {
        fprintf(stderr, "Failed to read fileID\n");
        fclose(f);
        exit(1);
    }
    header.fileID[4] = '\0';

    if (fread(header.typeID, 1, 4, f) != 4) {
        fprintf(stderr, "Failed to read typeID\n");
        fclose(f);
        exit(1);
    }
    header.typeID[4] = '\0';

    int c;
    c = fgetc(f);
    if (c == EOF) { perror("Unexpected EOF reading fileVersion"); fclose(f); exit(1); }
    header.fileVersion = (uint8_t)c;

    c = fgetc(f);
    if (c == EOF) { perror("Unexpected EOF reading playerVersion"); fclose(f); exit(1); }
    header.playerVersion = (uint8_t)c;

    header.pPlayer = read_be_uint16(f);
    header.pAuthor = read_be_uint16(f);
    header.pMisc = read_be_uint16(f);

    c = fgetc(f);
    if (c == EOF) { perror("Unexpected EOF reading numSongs"); fclose(f); exit(1); }
    header.numSongs = (uint8_t)c;

    c = fgetc(f);
    if (c == EOF) { perror("Unexpected EOF reading firstSong"); fclose(f); exit(1); }
    header.firstSong = (uint8_t)c;

    header.pSongsStructure = read_be_uint16(f);

    printf("FileID: %s | TypeID: %s\n", header.fileID, header.typeID);
    printf("FileVersion: %d | PlayerVersion: %d | NumOfSongs: %d | FirstSong: %d\n",
        header.fileVersion, header.playerVersion, header.numSongs + 1, header.firstSong + 1);

    // Read author and misc strings using offsets relative to file start + defined constants + header pointers
    char author[MAX_STR_LEN] = "";
    char misc[MAX_STR_LEN] = "";

    if (header.pAuthor != 0) {
        read_str_at(f, PAUTHOR_OFFSET + header.pAuthor, author, MAX_STR_LEN);
    }
    if (header.pMisc != 0) {
        read_str_at(f, PMISC_OFFSET + header.pMisc, misc, MAX_STR_LEN);
    }

    printf("Author: %s | Misc: %s\n", author, misc);

    // Base offset for songs structure
    long baseOffset = PSONGSSTRUCTURE_OFFSET + header.pSongsStructure;

    printf("\n-- Songs List and Data Blocks --\n");

    for (int i = 0; i <= header.numSongs; i++) {
        long songEntryOffset = baseOffset + (i * 4);
        if (fseek(f, songEntryOffset, SEEK_SET) != 0) {
            perror("Failed to seek to song entry");
            continue;
        }

        uint16_t pSongName = read_be_uint16(f);
        uint16_t pSongData = read_be_uint16(f);

        // Calculate song name offset correctly: baseOffset + pSongName + (i * 4)
        long songNameOffset = baseOffset + pSongName + (i * 4);
        char songTitle[MAX_STR_LEN];
        read_str_at(f, songNameOffset, songTitle, MAX_STR_LEN);

        printf("\nSong %d: %s (Data Ptr: 0x%04X)\n", i + 1, songTitle, pSongData);

        long songDataOffset = baseOffset + pSongData;
        if (fseek(f, songDataOffset, SEEK_SET) != 0) {
            perror("Failed to seek to song data");
            continue;
        }

        uint8_t aChan = (uint8_t)fgetc(f);
        uint8_t bChan = (uint8_t)fgetc(f);
        uint8_t cChan = (uint8_t)fgetc(f);
        uint8_t noise = (uint8_t)fgetc(f);
        uint16_t songLength = read_be_uint16(f);
        uint16_t fadeLength = read_be_uint16(f);
        uint8_t hiReg = (uint8_t)fgetc(f);
        uint8_t loReg = (uint8_t)fgetc(f);
        uint16_t pPoints = read_be_uint16(f);
        uint16_t pAddresses = read_be_uint16(f);

        printf("  Channels: A=%d B=%d C=%d Noise=%d\n", aChan, bChan, cChan, noise);
        printf("  SongLength=%d | FadeLength=%d | PointsPtr=0x%04X | AddrPtr=0x%04X\n",
            songLength, fadeLength, pPoints, pAddresses);

        long blockOffset = songDataOffset + pAddresses;

        printf("  Song Data Offset: 0x%lX | Blocks Table Offset: 0x%lX\n", songDataOffset, blockOffset);

        if (fseek(f, blockOffset, SEEK_SET) != 0) {
            perror("Failed to seek to song data blocks");
            continue;
        }

        long currentBlockOffset = blockOffset;
        int blockNum = 0;
        while (1) {
            if (fseek(f, currentBlockOffset, SEEK_SET) != 0) {
                perror("Failed to seek to block entry");
                break;
            }

            uint16_t addr = read_be_uint16(f);
            if (addr == 0) break;  // Only terminate on addr==0

            uint16_t length = read_be_uint16(f);
            uint16_t offset = read_be_uint16(f);

            printf("  Block %d: Addr=0x%04X Length=%d Offset=0x%04X (File Offset: 0x%lX)\n",
                ++blockNum, addr, length, offset, currentBlockOffset);

            currentBlockOffset += 6;

            if (blockNum >= MAX_BLOCKS) {
                printf("  Too many blocks, stopping.\n");
                break;
            }
        }
    }

    fclose(f);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <ayfile>\n", argv[0]);
        return 1;
    }

    list_songs_and_blocks(argv[1]);
    return 0;
}