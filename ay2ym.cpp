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

    char author[MAX_STR_LEN];
    char misc[MAX_STR_LEN];
    char songName[MAX_STR_LEN];

    uint8_t aChan, bChan, cChan, noise;
    uint16_t songLength, fadeLength;
    uint8_t hiReg, loReg;
    uint16_t pPoints, pAddresses;

    uint16_t stack, init, interrupt;

    DataBlock blocks[MAX_BLOCKS];
    int numBlocks;

} AYFile;

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

void parse_ay_file(const char* filename, AYFile* ay) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        perror("Error opening file");
        exit(1);
    }

    fread(ay->fileID, 1, 4, f); ay->fileID[4] = '\0';
    fread(ay->typeID, 1, 4, f); ay->typeID[4] = '\0';
    ay->fileVersion = fgetc(f);
    ay->playerVersion = fgetc(f);
    ay->pPlayer = read_be_uint16(f);
    ay->pAuthor = read_be_uint16(f);
    ay->pMisc = read_be_uint16(f);
    ay->numSongs = fgetc(f);
    ay->firstSong = fgetc(f);
    ay->pSongsStructure = read_be_uint16(f);

    read_str_at(f, 12 + ay->pAuthor, ay->author, MAX_STR_LEN);
    read_str_at(f, 14 + ay->pMisc, ay->misc, MAX_STR_LEN);

    long songStructOffset = 18 + ay->pSongsStructure;
    fseek(f, songStructOffset, SEEK_SET);
    uint16_t pSongName = read_be_uint16(f);
    uint16_t pSongData = read_be_uint16(f);
    read_str_at(f, 18 + ay->pSongsStructure + pSongName, ay->songName, MAX_STR_LEN);

    long songDataOffset = songStructOffset + 4;
    fseek(f, songDataOffset, SEEK_SET);
    ay->aChan = fgetc(f);
    ay->bChan = fgetc(f);
    ay->cChan = fgetc(f);
    ay->noise = fgetc(f);
    ay->songLength = read_be_uint16(f);
    ay->fadeLength = read_be_uint16(f);
    ay->hiReg = fgetc(f);
    ay->loReg = fgetc(f);
    ay->pPoints = read_be_uint16(f);
    ay->pAddresses = read_be_uint16(f);

    long ptrOffset = songDataOffset + 10 + ay->pPoints;
    fseek(f, ptrOffset, SEEK_SET);
    ay->stack = read_be_uint16(f);
    ay->init = read_be_uint16(f);
    ay->interrupt = read_be_uint16(f);

    ay->numBlocks = 0;
    long dataBlockOffset = ptrOffset + 6;
    fseek(f, dataBlockOffset, SEEK_SET);

    while (ay->numBlocks < MAX_BLOCKS) {
        DataBlock* blk = &ay->blocks[ay->numBlocks];
        int hi = fgetc(f);
        int lo = fgetc(f);
        if (hi == EOF || lo == EOF) break;
        blk->addr = (hi << 8) | lo;
        if (blk->addr == 0) break;

        hi = fgetc(f);
        lo = fgetc(f);
        if (hi == EOF || lo == EOF) break;
        blk->length = (hi << 8) | lo;
        if (blk->length == 0) break;

        hi = fgetc(f);
        lo = fgetc(f);
        if (hi == EOF || lo == EOF) break;
        blk->offset = (hi << 8) | lo;

        ay->numBlocks++;
    }

    fclose(f);
}

void print_ay_info(const AYFile* ay) {
    printf("FileID: %s | TypeID: %s\n", ay->fileID, ay->typeID);
    printf("FileVersion: %d | PlayerVersion: %d | NumOfSongs: %d | FirstSong: %d\n",
        ay->fileVersion, ay->playerVersion, ay->numSongs, ay->firstSong + 1);
    printf("Author: %s | Misc: %s\n", ay->author, ay->misc);
    printf("Song Title: %s\n", ay->songName);
    printf("Channels: A=%d B=%d C=%d Noise=%d | SongLength=%d | FadeLength=%d\n",
        ay->aChan, ay->bChan, ay->cChan, ay->noise, ay->songLength, ay->fadeLength);
    printf("Data Blocks: %d\n", ay->numBlocks);
    for (int i = 0; i < ay->numBlocks; ++i) {
        printf("  Block %d: Addr=0x%04X Length=%d Offset=0x%04X\n",
            i + 1, ay->blocks[i].addr, ay->blocks[i].length, ay->blocks[i].offset);
    }
}

void list_all_songs(const char* filename, const AYFile* ay) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        perror("Error opening file for song listing");
        return;
    }

    printf("\n-- Songs List --\n");

    long baseOffset = 18 + ay->pSongsStructure;

    for (int i = 0; i <= ay->numSongs; i++) {
        long songEntryOffset = baseOffset + (i * 4);
        fseek(f, songEntryOffset, SEEK_SET);

        uint16_t pSongName = read_be_uint16(f);
        uint16_t pSongData = read_be_uint16(f);

        long songNameOffset = baseOffset + pSongName + (i * 4);
        char songTitle[MAX_STR_LEN];
        read_str_at(f, songNameOffset, songTitle, MAX_STR_LEN);

        printf("Song %d: %s (Data Ptr: 0x%04X)\n", i + 1, songTitle, pSongData);
    }

    fclose(f);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <ayfile>\n", argv[0]);
        return 1;
    }

    AYFile ay;
    parse_ay_file(argv[1], &ay);
    print_ay_info(&ay);
    list_all_songs(argv[1], &ay);

    return 0;
}