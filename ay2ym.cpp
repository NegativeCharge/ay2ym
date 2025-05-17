#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STR_LEN 128
#define MAX_BLOCKS 256

typedef struct {
    uint16_t addr;
    uint16_t length;
    uint16_t offset;
} DataBlock;

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
    uint8_t hi = fgetc(f);
    uint8_t lo = fgetc(f);
    return (hi << 8) | lo;
}

void read_str_at(FILE* f, long offset, char* dest, size_t maxLen) {
    fseek(f, offset, SEEK_SET);
    size_t i = 0;
    int c;
    while (i < maxLen - 1 && (c = fgetc(f)) != EOF && c != 0) {
        dest[i++] = c;
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

    fread(header.fileID, 1, 4, f); header.fileID[4] = '\0';
    fread(header.typeID, 1, 4, f); header.typeID[4] = '\0';
    header.fileVersion = fgetc(f);
    header.playerVersion = fgetc(f);
    header.pPlayer = read_be_uint16(f);
    header.pAuthor = read_be_uint16(f);
    header.pMisc = read_be_uint16(f);
    header.numSongs = fgetc(f);
    header.firstSong = fgetc(f);
    header.pSongsStructure = read_be_uint16(f);

    printf("FileID: %s | TypeID: %s\n", header.fileID, header.typeID);
    printf("FileVersion: %d | PlayerVersion: %d | NumOfSongs: %d | FirstSong: %d\n",
        header.fileVersion, header.playerVersion, header.numSongs + 1, header.firstSong + 1);

    long baseOffset = 18 + header.pSongsStructure;

    printf("\n-- Songs List and Data Blocks --\n");

    for (int i = 0; i <= header.numSongs; i++) {
        long songEntryOffset = baseOffset + (i * 4);
        fseek(f, songEntryOffset, SEEK_SET);

        uint16_t pSongName = read_be_uint16(f);
        uint16_t pSongData = read_be_uint16(f);

        long songNameOffset = baseOffset + pSongName + (i * 4);
        char songTitle[MAX_STR_LEN];
        read_str_at(f, songNameOffset, songTitle, MAX_STR_LEN);

        printf("\nSong %d: %s (Data Ptr: 0x%04X)\n", i + 1, songTitle, pSongData);

        // Now read song data
        long songDataOffset = baseOffset + pSongData;
        fseek(f, songDataOffset, SEEK_SET);

        uint8_t aChan = fgetc(f);
        uint8_t bChan = fgetc(f);
        uint8_t cChan = fgetc(f);
        uint8_t noise = fgetc(f);
        uint16_t songLength = read_be_uint16(f);
        uint16_t fadeLength = read_be_uint16(f);
        uint8_t hiReg = fgetc(f);
        uint8_t loReg = fgetc(f);
        uint16_t pPoints = read_be_uint16(f);
        uint16_t pAddresses = read_be_uint16(f);

        printf("  Channels: A=%d B=%d C=%d Noise=%d\n", aChan, bChan, cChan, noise);
        printf("  SongLength=%d | FadeLength=%d | PointsPtr=0x%04X | AddrPtr=0x%04X\n",
            songLength, fadeLength, pPoints, pAddresses);

        // Now read Data Blocks
        long blockOffset = songDataOffset + 10 + pPoints + 6;
        fseek(f, blockOffset, SEEK_SET);

        int blockNum = 0;
        while (1) {
            uint16_t addr = read_be_uint16(f);
            if (addr == 0 || feof(f)) break;

            uint16_t length = read_be_uint16(f);
            if (length == 0 || feof(f)) break;

            uint16_t offset = read_be_uint16(f);

            printf("  Block %d: Addr=0x%04X Length=%d Offset=0x%04X\n",
                ++blockNum, addr, length, offset);
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