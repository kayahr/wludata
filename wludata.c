/*
 * $Id$
 * Copyright (C) 2007  Klaus Reimer <k@ailis.de>
 * See COPYING file for copying conditions
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct hNode_s
{
    struct hNode_s *parent;
    struct hNode_s *left;
    struct hNode_s *right;
    unsigned char payload;
    int key;
    char keyBits;
    int usage;
} hNode;

static void die(char *message, ...)
{
    va_list args;
    
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);
    exit(1);
}

static int readBit(FILE *file, unsigned char *dataByte, unsigned char *dataMask)
{
    int tmp;
    
    if (*dataMask == 0)
    {
        fread(dataByte, 1, 1, file);
        *dataMask = 0x80;
    }
    tmp = *dataByte & *dataMask;
    *dataMask = *dataMask >> 1;
    return tmp ? 1 : 0;
}

static int readByte(FILE *file, unsigned char *dataByte, unsigned char *dataMask)
{
    int byte, i, bit;
 
    byte = 0;
    for (i = 0 ; i < 8 ; i++)
    {
        bit = readBit(file, dataByte, dataMask);
        byte = (byte << 1) | bit;
    }
    return byte;
}


static unsigned long read32(FILE *file)
{
    return fgetc(file) | (fgetc(file) << 8) | (fgetc(file) << 16)
        | (fgetc(file) << 24);
}

static hNode * readHuffmanNode(FILE *file, unsigned char *dataByte,
        unsigned char *dataMask)
{
    hNode *node, *left, *right;    
    int bit, payload;
    
    // Read payload or sub nodes. 
    bit = readBit(file, dataByte, dataMask);
    if (bit)
    {
        left = NULL;
        right = NULL;
        payload = readByte(file, dataByte, dataMask);
    }
    else
    {
        left = readHuffmanNode(file, dataByte, dataMask);
        readBit(file, dataByte, dataMask);
        right = readHuffmanNode(file, dataByte, dataMask);
        payload = 0;
    }
    
    // Build and return the node
    node = (hNode *) malloc(sizeof(hNode));
    if (!node) die("Out of memory\n");
    node->left = left;
    node->right = right;
    node->payload = payload;
    return node;
}

static void freeHuffmanNode(hNode *node)
{
    if (node->left != NULL) freeHuffmanNode(node->left);
    if (node->right != NULL) freeHuffmanNode(node->right);        
    free(node);
}

static unsigned char readHuffmanByte(FILE *file, hNode *rootNode,
        unsigned char *dataByte, unsigned char *dataMask)
{
    int bit;
    hNode *node;
    
    node = rootNode;
    while (node->left != NULL)
    {
        bit = readBit(file, dataByte, dataMask);
        node = bit ? node->right : node->left; 
    }
    return node->payload;
}

static void decodeVXOR(unsigned char *data, int width, int height)
{
    int x, y;
    unsigned char xor;
    
    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            xor = y == 0 ? 0 : data[(y - 1) * width + x];
            data[y * width + x] ^= xor;
        }
    }
}

static void unpackHtds()
{
    FILE *file, *out;
    char *filename, outfilename[16];
    int fileNo;
    unsigned long size;
    hNode *rootNode;
    int i, id;
    unsigned char dataByte, dataMask;
    unsigned char data[20864];
    unsigned char magic[4];
    
    id = 0;
    for (fileNo = 0; fileNo < 2; fileNo++)
    {
        filename = fileNo ? "allhtds2" : "allhtds1";
        file = fopen(filename, "rb");
        if (!file) die("Unable to open %s: %s\n", filename, strerror(errno));
        
        while (!feof(file))
        {
            // Read size of uncompressed MSQ block
            size = read32(file);
            if (feof(file)) break;
            
            // Read and validate MSQ header
            fread(magic, 4, 1, file);
            if (magic[0] != 'm' || magic[1] != 's' || magic[2] != 'q'
                    || magic[3] != fileNo)
                die ("Invalid file: %s\n", filename);
            
            // Read huffman tree
            dataByte = 0;
            dataMask = 0;
            rootNode = readHuffmanNode(file, &dataByte, &dataMask);
            
            // Read data
            memset(data, 0, sizeof(data));
            for (i = 0; i < size; i++)
            {
                data[i] = readHuffmanByte(file, rootNode, &dataByte, &dataMask);
            }
            
            // Decode data
            for (i = 0; i < size; i += 16 * 16 / 2)
            {
                decodeVXOR(&data[i], 8, 16);
            }
            
            // Write data
            sprintf(outfilename, "tile%02i", id);
            out = fopen(outfilename, "wb");
            fwrite(data, 1, sizeof(data), out);
            fclose(out);
            
            // Free resources and continue with next tileset
            freeHuffmanNode(rootNode);
            id++;
        }
        fclose(file);
    }
}

static unsigned char *readRotateXOR(FILE *stream, int size, int encSize)
{
    unsigned char *data;
    unsigned char byte1, byte2, enc, crypt, plain;
    unsigned short checksum;
    int i;
    
    data = (unsigned char *) malloc(size);
    if (!data) die("Out of memory\n");
    byte1 = fgetc(stream);
    byte2 = fgetc(stream);
    enc = byte1 ^ byte2;
    checksum = 0;
    i = 0;
    while (i < (encSize >=0 ? encSize : size))
    {
        crypt = fgetc(stream);
        plain = crypt ^ enc;
        data[i] = plain;
        i++;
        checksum = checksum - plain;
        if (encSize == -1 && checksum == ((int) byte2 << 8 | byte1)) break;
        enc += 0x1f;
    }
    while (i < size)
    {
        data[i] = fgetc(stream);
        i++;
    }
    return data;
}

static void unpackPics()
{
    FILE *file, *out;
    char *filename, outfilename[16];
    int fileNo;
    unsigned long size;
    hNode *rootNode;
    int i, id;
    unsigned char dataByte, dataMask;
    unsigned char *data;
    unsigned char magic[4];
    int trans[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 16, 17, 38, 42,
            43, 44, 45, 46, 47, 48, 49, 50, 52, 54, 55, 56, 58, 59, 60, 78, 0,
            1, 2, 4, 8, 12, 14, 15, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
            29, 30, 31, 32, 33, 34, 35, 36, 37, 39, 40, 41, 45, 57, 61, 62, 64,
            65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77 };
    
    id = 0;
       
    for (fileNo = 0; fileNo < 2; fileNo++)
    {
        filename = fileNo ? "allpics2" : "allpics1";
        file = fopen(filename, "rb");
        if (!file) die("Unable to open %s: %s\n", filename, strerror(errno));
        
        while (!feof(file))
        {
            // Read size of uncompressed MSQ block
            size = read32(file);
            if (feof(file)) break;
            
            // Read and validate MSQ header
            fread(magic, 4, 1, file);
            if (magic[0] != 'm' || magic[1] != 's' || magic[2] != 'q')
                die ("Invalid file: %s\n", filename);
            
            // Read huffman tree for base frame
            dataByte = 0;
            dataMask = 0;
            rootNode = readHuffmanNode(file, &dataByte, &dataMask);
            
            // Read base frame
            data = (unsigned char *) malloc(size);
            if (!data) die("Out of memory\n");
            for (i = 0; i < size; i++)
            {
                data[i] = readHuffmanByte(file, rootNode, &dataByte, &dataMask);
            }
            decodeVXOR(data, 96 / 2, 84);
            
            // Write base frame data
            sprintf(outfilename, "pic%02i", trans[id]);
            out = fopen(outfilename, "wb");
            fwrite(data, 1, size, out);
            
            // Free resources
            free(data);
            freeHuffmanNode(rootNode);
            
            // Read size of uncompressed MSQ block (Animation part)
            size = read32(file);
            
            // Read and validate MSQ header
            fread(magic, 4, 1, file);
            if (magic[0] != 'm' || magic[1] != 's' || magic[2] != 'q')
                die ("Invalid file: %s\n", filename);
            
            // Read huffman tree for animation part
            dataByte = 0;
            dataMask = 0;
            rootNode = readHuffmanNode(file, &dataByte, &dataMask);
            
            // Read base frame
            data = (unsigned char *) malloc(size);
            if (!data) die("Out of memory\n");
            for (i = 0; i < size; i++)
            {
                data[i] = readHuffmanByte(file, rootNode, &dataByte, &dataMask);
            }
            
            // Write decoded animation part and close file
            fwrite(data, 1, size, out);
            fclose(out);

            // Free resources and continue with next tileset
            free(data);
            freeHuffmanNode(rootNode);
            id++;
        }
        fclose(file);
    }
}

static void unpackGame()
{
    FILE *file;
    FILE *out;
    char *filename;
    char outFilename[16];
    int fileNo;
    unsigned long size;
    hNode *rootNode;
    int i;
    int b;
    int mapSize;
    unsigned char dataByte, dataMask;
    unsigned char *data;
    unsigned char magic[4];    
    long game1Offsets[] = { 0, 10958, 16697, 26297, 36394, 44582, 49112, 59252,
            66522, 73455, 83203, 93710, 100598, 105823, 110061, 115498, 122394,
            131766, 139891, 145679 }; 
    long game2Offsets[] = { 0, 4320, 9598, 21955, 29760, 35167, 40852, 46793,
            51745, 58006, 66901, 72935, 84078, 94274, 102058, 109750, 118668,
            127156, 137175, 142703, 151754, 160761 };
    int game1MapSizes[] = { 64, 32, 32, 32, 32, 32, 32, 32, 32, 32, 64, 32, 32, 
            32, 32, 32, 32, 32, 32, 32 };    
    int game2MapSizes[] = { 32, 32, 64, 32, 32, 32, 32, 32, 32, 32, 32, 64, 32, 
            32, 32, 32, 32, 32, 32, 32, 32, 32 };
    long game1TileMapOffsets[] = { 8577, 4991, 9217, 9567, 7732, 4175, 9630,
            6823, 6572, 9218, 8464, 6251, 4696, 3852, 5094, 6333, 8777,
            7655, 5428, 6313 };
    long game2TileMapOffsets[] = { 3963, 4885, 9664, 7356, 5016, 5224, 5563,
            4486, 5894, 8550, 5426, 9680, 9696, 7337, 7329, 8460, 8132, 9377, 
            5153, 8711, 8512, 5656, };
    int game1Ids[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 26, 27, 28, 29, 31, 32,
        33, 34, 43, 49 };
    int game2Ids[] = { 7, 11, 12, 13, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, 35, 36, 38, 39, 40, 41, 42 };
    int game1EncSizes[] = { 7265, 3085, 3401, 3884, 4593, 2207, 4772, 3049,
            2937, 4100, 7050, 3435, 2815, 2334, 2349, 3440, 4048, 3502, 3221,
            3168 };
    int game2EncSizes[] = { 2173, 2109, 7997, 4074, 2448, 3135, 2597, 2944,
            3476, 4645, 3372, 7686, 3814, 3672, 3388, 3918, 3945, 4846, 2711,
            3820, 4585, 2752 };
    long *offsets, *tileMapOffsets;
    int *ids, *mapSizes, *encSizes;
    int map, id, encSize;
    long offset;
    
    for (fileNo = 0; fileNo < 2; fileNo++)
    {
        filename = fileNo ? "game2" : "game1";
        offsets = fileNo ? game2Offsets : game1Offsets;
        mapSizes = fileNo ? game2MapSizes : game1MapSizes;
        encSizes = fileNo ? game2EncSizes : game1EncSizes;
        tileMapOffsets = fileNo ? game2TileMapOffsets : game1TileMapOffsets;
        ids = fileNo ? game2Ids : game1Ids;
        
        file = fopen(filename, "rb");
        if (!file) die("Unable to open %s: %s\n", filename, strerror(errno));
        
        for (map = 0; map < (fileNo ? 22 : 20); map++)
        {
            offset = offsets[map];
            mapSize = mapSizes[map];
            size = tileMapOffsets[map];
            encSize = encSizes[map];
            id = ids[map];
            
            // Read and validate MSQ header
            fseek(file, offset, SEEK_SET); 
            fread(magic, 1, 4, file);
            if (magic[0] != 'm' || magic[1] != 's' || magic[2] != 'q')
                die ("Invalid file: %s\n", filename);
            
            sprintf(outFilename, "map%02d", id);
            out = fopen(outFilename, "wb");
            fputc(size & 0xff, out);
            fputc(size >> 8, out);
            fputc(mapSize, out);

            data = readRotateXOR(file, size, encSize);
            fwrite(data, 1, size, out);
            free(data);
            
            size = read32(file);
            fseek(file, 4, SEEK_CUR);
            dataByte = 0;
            dataMask = 0;
            rootNode = readHuffmanNode(file, &dataByte, &dataMask);
            for (i = 0; i < size; i++)
            {
                b = readHuffmanByte(file, rootNode, &dataByte, &dataMask);
                fputc(b, out);
            }
            freeHuffmanNode(rootNode);
            fclose(out);
        }
        fclose(file);
    }
}

static void unpackItems()
{
    FILE *file;
    FILE *out;
    char *filename;
    char outFilename[16];
    int fileNo;
    unsigned char *data;
    unsigned char magic[4];    
    long game1Offsets[] = { 0x265cb, 0x268c9, 0x26bc7 };
    long game2Offsets[] = { 0x29dcd };
    int id;
    long offset;
    long *offsets;
    
    for (fileNo = 0; fileNo < 2; fileNo++)
    {
        filename = fileNo ? "game2" : "game1";
        offsets = fileNo ? game2Offsets : game1Offsets;
        
        file = fopen(filename, "rb");
        if (!file) die("Unable to open %s: %s\n", filename, strerror(errno));
        
        for (id = 0; id < (fileNo ? 1 : 3); id++)
        {
            offset = offsets[id];
            
            // Read and validate MSQ header
            fseek(file, offset, SEEK_SET); 
            fread(magic, 1, 4, file);
            if (magic[0] != 'm' || magic[1] != 's' || magic[2] != 'q')
                die ("Invalid file: %s\n", filename);
            
            sprintf(outFilename, "itm%i", fileNo ? id + 4 : id);
            out = fopen(outFilename, "wb");
            data = readRotateXOR(file, 760, 760);
            fwrite(data, 1, 760, out);
            free(data);
            fclose(out);
        }
        fclose(file);
    }
}

static void unpackSave()
{
    FILE *file;
    unsigned char *data;
    unsigned char magic[4];    
    
    file = fopen("game1", "rb");
    fseek(file, 0x253c5, SEEK_SET);
    fread(magic, 1, 4, file);
    if (magic[0] != 'm' || magic[1] != 's' || magic[2] != 'q')
        die ("Invalid file: game1\n");
    data = readRotateXOR(file, 4608, 0x800);
    fclose(file);
    
    file = fopen("save", "wb");
    fwrite(data, 1, 4608, file);
    fclose(file);
    
    free(data);
}

int main(void)
{
    printf("Converting original data files for hacked EXE...\n");
    unpackPics();
    unpackHtds();
    unpackGame();
    unpackItems();
    unpackSave();
    printf("Finished\n");
    return 0;
}
