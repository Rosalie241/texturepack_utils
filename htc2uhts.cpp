#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <zlib.h>
#include <unordered_map>
#include <ctype.h>

struct GHQTexInfo
{
    unsigned char* data;
    int width;
    int height;
    unsigned int format;
    unsigned short texture_format;
    unsigned short pixel_type;
    unsigned char is_hires_tex;
};

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("Usage: %s [HTC FILE]\n", argv[0]);
        return 1;
    }

    char inFilename[200];
    char outFilename[200];

    strcpy(inFilename, argv[1]);
    strcpy(outFilename, inFilename);

    /* make sure the end of inFileame 
     *  contains .htc, if it does, 
     *  overwrite it with .hts
     */
    char* inFileExtension = outFilename + strlen(outFilename) - 4;
    /* tolower extension */
    for (int i = 0; i < 4; i++) { inFileExtension[i] = tolower(inFileExtension[i]); }
    if (strcmp(inFileExtension, ".htc") != 0)
    {
        fprintf(stderr, "file doesn't end with .htc!\n");
        return 1;
    }

    /* overwrite .htc with .hts */
    strcpy(inFileExtension, ".hts");


    /* try to open provided filename */
    gzFile gzfp = gzopen(inFilename, "rb");
    if (gzfp == NULL)
    {
        perror("gzopen");
        return 1;
    }

    FILE* outFile = fopen(outFilename, "wb+");
    if (outFile == NULL)
    {
        perror("fopen");
        return 1;
    }

    /* write header to outFile */
    int32_t outConfig = 1075970048, mapOffset = -1;
    fwrite(&outConfig, 4, 1, outFile);
    fwrite(&mapOffset, 4, 1, outFile);

    /* read header (skip for now) */
    gzseek(gzfp, 4, SEEK_CUR);
    
    std::unordered_map<uint64_t, int64_t> mapping;

    /* keep reading until the end */
    while (!gzeof(gzfp))
    {
        int32_t dataSize;
        uint64_t checksum;
        struct GHQTexInfo info;

        gzread(gzfp, &checksum, 8);
        gzread(gzfp, &info.width, 4);
        gzread(gzfp, &info.height, 4);
        gzread(gzfp, &info.format, 4);
        gzread(gzfp, &info.texture_format, 2);
        gzread(gzfp, &info.pixel_type, 2);
        gzread(gzfp, &info.is_hires_tex, 1);
        gzread(gzfp, &dataSize, 4);
        
        info.data = (uint8_t*)malloc(dataSize);
        if (info.data == NULL)
        {
            fprintf(stderr, "malloc failed!\n");
            return 1;
        }
        
        gzread(gzfp, info.data, dataSize);

        printf("adding texture %08X %08X to %s\n", (uint32_t)(checksum & 0xffffffff), (uint32_t)(checksum >> 32), outFilename);

        /* add to mapping list */
        mapping.insert(std::make_pair(checksum, ftell(outFile)));

        /* write texture data to file */
#define FWRITE(x) fwrite(&x, sizeof(x), 1, outFile)
        FWRITE(info.width);
        FWRITE(info.height);
        FWRITE(info.format);
        FWRITE(info.texture_format);
        FWRITE(info.pixel_type);
        FWRITE(info.is_hires_tex);
        FWRITE(dataSize);
#undef FWRITE
        fwrite(info.data, dataSize, 1, outFile);

        /* free malloc'd data */
        free(info.data);
    }

    gzclose(gzfp);

    /* add mapping to HTS */
    printf("adding mapping to %s\n", outFilename);

#define FWRITE(x) fwrite(&x, sizeof(x), 1, outFile)
    int64_t mappingOffset = ftell(outFile);
    int32_t mappingSize = (int32_t)mapping.size();
    FWRITE(mappingSize);
    for (auto item : mapping)
    {
        FWRITE(item.first);
        FWRITE(item.second);
    }

    /* write mapping offset */
    fseek(outFile, 4L, SEEK_SET);
    FWRITE(mappingOffset);
#undef FWRITE

    fclose(outFile);

    printf("completed\n");

    return 0;
}
